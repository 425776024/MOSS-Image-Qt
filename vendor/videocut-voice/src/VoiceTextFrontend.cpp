#include "VoiceTextFrontend.h"

#include <CoreFoundation/CoreFoundation.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace videocut::ai::native::detail {
namespace {

using Json = nlohmann::json;

struct CodePoint final {
  std::uint32_t value{0};
  std::string bytes;
};

std::vector<CodePoint> DecodeUtf8(const std::string &text) {
  std::vector<CodePoint> output;
  for (std::size_t offset = 0; offset < text.size();) {
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    std::uint32_t value = first;
    if ((first & 0xE0U) == 0xC0U) {
      length = 2;
      value = first & 0x1FU;
    } else if ((first & 0xF0U) == 0xE0U) {
      length = 3;
      value = first & 0x0FU;
    } else if ((first & 0xF8U) == 0xF0U) {
      length = 4;
      value = first & 0x07U;
    } else if ((first & 0x80U) != 0) {
      throw std::invalid_argument("voice tokenizer input is not valid UTF-8");
    }
    if (offset + length > text.size())
      throw std::invalid_argument("voice tokenizer input ends inside UTF-8");
    for (std::size_t index = 1; index < length; ++index) {
      const auto continuation =
          static_cast<unsigned char>(text[offset + index]);
      if ((continuation & 0xC0U) != 0x80U)
        throw std::invalid_argument(
            "voice tokenizer input has invalid UTF-8 continuation");
      value = (value << 6U) | (continuation & 0x3FU);
    }
    if ((length == 2 && value < 0x80U) || (length == 3 && value < 0x800U) ||
        (length == 4 && value < 0x10000U) || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
      throw std::invalid_argument(
          "voice tokenizer input has non-canonical UTF-8");
    }
    output.push_back(CodePoint{
        value, text.substr(offset, static_cast<std::size_t>(length))});
    offset += length;
  }
  return output;
}

bool IsWhitespace(const std::uint32_t value) {
  return value == 0x20U || (value >= 0x09U && value <= 0x0DU) ||
         value == 0x85U || value == 0xA0U || value == 0x1680U ||
         (value >= 0x2000U && value <= 0x200AU) || value == 0x2028U ||
         value == 0x2029U || value == 0x202FU || value == 0x205FU ||
         value == 0x3000U;
}

bool IsMember(const CFCharacterSetPredefinedSet set,
              const std::uint32_t value) {
  return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(set),
                                             static_cast<UTF32Char>(value)) !=
         0;
}

bool IsUnicodeNumber(const std::uint32_t value) {
  // Hugging Face tokenizers' Digits pre-tokenizer uses Unicode \p{N}.
  // CoreFoundation's alphanumeric set is L* + M* + N*, while its letter set
  // is L* + M*, so the set difference is exactly N*.
  return IsMember(kCFCharacterSetAlphaNumeric, value) &&
         !IsMember(kCFCharacterSetLetter, value);
}

bool IsConnectorPunctuation(const std::uint32_t value) {
  // Unicode General_Category=Connector_Punctuation (Pc).
  return value == 0x005FU || value == 0x203FU || value == 0x2040U ||
         value == 0x2054U || value == 0xFE33U || value == 0xFE34U ||
         (value >= 0xFE4DU && value <= 0xFE4FU) || value == 0xFF3FU;
}

bool IsRegexWord(const std::uint32_t value) {
  // Rust regex \w (used by tokenizers) includes alphabetic/mark/number
  // characters, connector punctuation, and Join_Control.
  return IsMember(kCFCharacterSetAlphaNumeric, value) ||
         IsConnectorPunctuation(value) || value == 0x200CU || value == 0x200DU;
}

enum class PieceClass : std::uint8_t { Whitespace, Word, Punctuation };

PieceClass Classify(const CodePoint &value) {
  if (IsWhitespace(value.value))
    return PieceClass::Whitespace;
  return IsRegexWord(value.value) ? PieceClass::Word : PieceClass::Punctuation;
}

std::uint8_t PreferredChunkBoundary(const CodePoint &point) {
  switch (point.value) {
  case '\n':
  case '\r':
  case '.':
  case '!':
  case '?':
  case ';':
  case 0x0085U:
  case 0x2028U:
  case 0x2029U:
  case 0x2026U:
  case 0x3002U:
  case 0xFF01U:
  case 0xFF1FU:
  case 0xFF1BU:
    return 3;
  case ',':
  case ':':
  case 0x3001U:
  case 0xFF0CU:
  case 0xFF1AU:
    return 2;
  default:
    return IsWhitespace(point.value) ? 1 : 0;
  }
}

std::string UnicodeLowercase(const std::string &text) {
  // Hugging Face's Lowercase normalizer follows Rust char::to_lowercase:
  // every Unicode scalar is mapped independently. Lowercasing the complete
  // CFString would apply contextual rules (for example, "ΟΣ" -> "ος"), while
  // the locked tokenizer expects the scalar mapping "ΟΣ" -> "οσ".
  const std::vector<CodePoint> points = DecodeUtf8(text);
  std::unordered_map<std::uint32_t, std::string> scalarCache;
  scalarCache.reserve(std::min<std::size_t>(points.size(), 256));
  std::string output;
  output.reserve(text.size());
  const CFLocaleRef locale =
      CFLocaleCreate(kCFAllocatorDefault, CFSTR("en_US_POSIX"));
  if (locale == nullptr)
    throw std::runtime_error("unable to allocate voice lowercase locale");
  try {
    for (const CodePoint &point : points) {
      const auto cached = scalarCache.find(point.value);
      if (cached != scalarCache.end()) {
        output.append(cached->second);
        continue;
      }

      const CFStringRef source = CFStringCreateWithBytes(
          kCFAllocatorDefault,
          reinterpret_cast<const UInt8 *>(point.bytes.data()),
          static_cast<CFIndex>(point.bytes.size()), kCFStringEncodingUTF8,
          false);
      if (source == nullptr)
        throw std::runtime_error("unable to allocate voice lowercase scalar");
      CFMutableStringRef lowered =
          CFStringCreateMutableCopy(kCFAllocatorDefault, 0, source);
      CFRelease(source);
      if (lowered == nullptr)
        throw std::runtime_error("unable to allocate normalized voice scalar");

      CFStringLowercase(lowered, locale);
      const CFIndex length = CFStringGetLength(lowered);
      const CFIndex capacity =
          CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
      if (capacity < 0) {
        CFRelease(lowered);
        throw std::runtime_error("unable to size normalized voice scalar");
      }
      std::string mapped;
      try {
        mapped.resize(static_cast<std::size_t>(capacity), '\0');
      } catch (...) {
        CFRelease(lowered);
        throw;
      }
      CFIndex used = 0;
      const CFIndex converted = CFStringGetBytes(
          lowered, CFRangeMake(0, length), kCFStringEncodingUTF8, 0, false,
          reinterpret_cast<UInt8 *>(mapped.data()), capacity, &used);
      CFRelease(lowered);
      if (converted != length)
        throw std::runtime_error("unable to encode normalized voice scalar");
      mapped.resize(static_cast<std::size_t>(used));
      const auto inserted = scalarCache.emplace(point.value, std::move(mapped));
      output.append(inserted.first->second);
    }
  } catch (...) {
    CFRelease(locale);
    throw;
  }
  CFRelease(locale);
  return output;
}

std::string Normalize(const std::string &text) {
  const std::string lowered = UnicodeLowercase(text);
  std::string normalized;
  bool inWhitespace = false;
  for (const CodePoint &point : DecodeUtf8(lowered)) {
    if (IsWhitespace(point.value)) {
      if (!inWhitespace)
        normalized.push_back(' ');
      inWhitespace = true;
      continue;
    }
    inWhitespace = false;
    normalized.append(point.bytes);
  }
  return normalized;
}

std::string PairKey(const std::string &left, const std::string &right) {
  std::string key;
  key.reserve(left.size() + right.size() + 1);
  key.append(left);
  key.push_back('\0');
  key.append(right);
  return key;
}

bool IsDecimal(const std::string_view value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char c) {
           return std::isdigit(c) != 0;
         });
}

} // namespace

VoiceTextFrontend::VoiceTextFrontend(
    const std::filesystem::path &tokenizerJson) {
  std::ifstream input(tokenizerJson, std::ios::binary);
  if (!input)
    throw std::runtime_error("unable to open voice tokenizer.json");
  Json root = Json::parse(input, nullptr, false);
  if (root.is_discarded() || !root.is_object() ||
      root.value("version", std::string{}) != "1.0" ||
      !root.contains("model") || !root["model"].is_object() ||
      root["model"].value("type", std::string{}) != "BPE") {
    throw std::runtime_error("unsupported voice tokenizer format");
  }

  const Json &model = root["model"];
  if (!model.contains("vocab") || !model["vocab"].is_object() ||
      !model.contains("merges") || !model["merges"].is_array()) {
    throw std::runtime_error("voice tokenizer has no BPE vocabulary/merges");
  }
  std::int64_t maximumId = -1;
  for (auto iterator = model["vocab"].begin(); iterator != model["vocab"].end();
       ++iterator) {
    const std::int64_t id = iterator.value().get<std::int64_t>();
    m_vocab.emplace(iterator.key(), id);
    maximumId = std::max(maximumId, id);
  }
  if (root.contains("added_tokens") && root["added_tokens"].is_array()) {
    for (const Json &item : root["added_tokens"]) {
      const std::string content = item.value("content", std::string{});
      const std::int64_t id = item.value("id", std::int64_t{-1});
      if (content.empty() || id < 0)
        throw std::runtime_error("voice tokenizer added token is invalid");
      m_vocab[content] = id;
      maximumId = std::max(maximumId, id);
    }
  }
  m_vocabularySize = static_cast<std::size_t>(maximumId + 1);
  m_audioCodeByTokenId.assign(m_vocabularySize, -1);
  for (const auto &[token, id] : m_vocab) {
    if (token.size() < 3 || token.front() != '[' || token.back() != ']')
      continue;
    const std::string_view digits(token.data() + 1, token.size() - 2);
    if (!IsDecimal(digits))
      continue;
    std::uint64_t code = 0;
    for (const char character : digits) {
      code = code * 10U +
             static_cast<std::uint64_t>(character - static_cast<char>('0'));
      if (code > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("voice audio token code overflows");
      }
    }
    m_audioCodeByTokenId[static_cast<std::size_t>(id)] =
        static_cast<std::int64_t>(code);
  }

  std::size_t rank = 0;
  for (const Json &merge : model["merges"]) {
    if (!merge.is_array() || merge.size() != 2)
      throw std::runtime_error("voice BPE merge entry is invalid");
    m_mergeRanks.emplace(
        PairKey(merge[0].get<std::string>(), merge[1].get<std::string>()),
        rank++);
  }
  m_unknownId = IdForToken("[UNK]");
  const auto targetEnd = m_vocab.find("[target_speech_end]");
  m_speechEndId = targetEnd != m_vocab.end() ? targetEnd->second
                                             : IdForToken("[speech_end]");
}

std::int64_t VoiceTextFrontend::IdForToken(const std::string &token) const {
  const auto found = m_vocab.find(token);
  if (found == m_vocab.end())
    throw std::runtime_error("voice tokenizer token is missing: " + token);
  return found->second;
}

std::optional<std::int64_t>
VoiceTextFrontend::AudioCodeForId(const std::int64_t tokenId) const {
  if (tokenId < 0 ||
      static_cast<std::size_t>(tokenId) >= m_audioCodeByTokenId.size())
    return std::nullopt;
  const std::int64_t code =
      m_audioCodeByTokenId[static_cast<std::size_t>(tokenId)];
  return code < 0 ? std::nullopt : std::optional<std::int64_t>(code);
}

std::int64_t VoiceTextFrontend::SpeechEndId() const noexcept {
  return m_speechEndId;
}

std::size_t VoiceTextFrontend::VocabularySize() const noexcept {
  return m_vocabularySize;
}

std::vector<VoiceTextChunk> VoiceTextFrontend::PlanTargetChunks(
    const std::string &text, const std::size_t maximumTargetTokens) const {
  if (text.empty())
    throw std::invalid_argument("voice synthesis target text is empty");
  if (maximumTargetTokens == 0)
    throw std::invalid_argument(
        "voice synthesis target token budget is empty");

  const std::vector<CodePoint> codePoints = DecodeUtf8(text);
  if (codePoints.empty())
    throw std::invalid_argument("voice synthesis target text is empty");

  std::vector<std::size_t> byteOffsets(codePoints.size() + 1U, 0);
  std::vector<std::uint8_t> boundaryPriorities(codePoints.size() + 1U, 0);
  for (std::size_t index = 0; index < codePoints.size(); ++index) {
    byteOffsets[index + 1U] =
        byteOffsets[index] + codePoints[index].bytes.size();
    boundaryPriorities[index + 1U] =
        PreferredChunkBoundary(codePoints[index]);
  }

  const auto encodeRange = [&](const std::size_t begin,
                               const std::size_t end) {
    const std::string slice =
        text.substr(byteOffsets[begin], byteOffsets[end] - byteOffsets[begin]);
    return std::make_pair(slice, Encode(slice).size());
  };

  auto whole = encodeRange(0, codePoints.size());
  if (whole.second <= maximumTargetTokens) {
    return {VoiceTextChunk{std::move(whole.first), whole.second,
                           codePoints.size()}};
  }

  std::vector<VoiceTextChunk> chunks;
  std::size_t begin = 0;
  while (begin < codePoints.size()) {
    std::size_t low = begin + 1U;
    std::size_t high = codePoints.size();
    std::size_t maximumEnd = begin;
    while (low <= high) {
      const std::size_t middle = low + (high - low) / 2U;
      const std::size_t tokenCount = encodeRange(begin, middle).second;
      if (tokenCount <= maximumTargetTokens) {
        maximumEnd = middle;
        low = middle + 1U;
      } else {
        high = middle - 1U;
      }
    }
    if (maximumEnd == begin) {
      throw std::invalid_argument(
          "voice synthesis cannot split one UTF-8 character within the target "
          "token budget");
    }

    std::size_t end = maximumEnd;
    const std::size_t preferredFloor =
        begin + std::max<std::size_t>(1U, (maximumEnd - begin) / 2U);
    for (std::uint8_t priority = 3; priority != 0; --priority) {
      for (std::size_t candidate = maximumEnd; candidate > preferredFloor;
           --candidate) {
        if (boundaryPriorities[candidate] == priority) {
          end = candidate;
          break;
        }
      }
      if (end != maximumEnd ||
          boundaryPriorities[maximumEnd] == priority) {
        break;
      }
    }

    auto encoded = encodeRange(begin, end);
    if (encoded.second > maximumTargetTokens) {
      // BPE token counts are not strictly monotonic as a suffix is extended.
      // The selected sentence boundary must still satisfy the hard budget.
      end = maximumEnd;
      encoded = encodeRange(begin, end);
    }
    if (encoded.second > maximumTargetTokens || encoded.first.empty()) {
      throw std::invalid_argument(
          "voice synthesis text chunk exceeds its tokenizer budget");
    }
    chunks.push_back(VoiceTextChunk{
        std::move(encoded.first), encoded.second, end - begin});
    begin = end;
  }
  return chunks;
}

std::pair<VoiceTextChunk, VoiceTextChunk>
VoiceTextFrontend::BisectTargetChunk(const std::string &text) const {
  if (text.empty())
    throw std::invalid_argument("voice synthesis target text is empty");
  const std::vector<CodePoint> codePoints = DecodeUtf8(text);
  if (codePoints.size() < 2U) {
    throw std::invalid_argument(
        "voice synthesis cannot bisect a single UTF-8 code point");
  }

  std::vector<std::size_t> byteOffsets(codePoints.size() + 1U, 0);
  std::vector<std::uint8_t> boundaryPriorities(codePoints.size() + 1U, 0);
  for (std::size_t index = 0; index < codePoints.size(); ++index) {
    byteOffsets[index + 1U] =
        byteOffsets[index] + codePoints[index].bytes.size();
    boundaryPriorities[index + 1U] =
        PreferredChunkBoundary(codePoints[index]);
  }

  const std::size_t middle = codePoints.size() / 2U;
  const std::size_t balancedRadius =
      std::max<std::size_t>(1U, codePoints.size() / 4U);
  const std::size_t firstCandidate =
      std::max<std::size_t>(1U, middle > balancedRadius
                                    ? middle - balancedRadius
                                    : 1U);
  const std::size_t lastCandidate =
      std::min<std::size_t>(codePoints.size() - 1U,
                            middle + balancedRadius);
  std::size_t split = middle;
  std::uint8_t bestPriority = 0;
  std::size_t bestDistance = std::numeric_limits<std::size_t>::max();
  for (std::size_t candidate = firstCandidate;
       candidate <= lastCandidate; ++candidate) {
    const std::uint8_t priority = boundaryPriorities[candidate];
    const std::size_t distance =
        candidate > middle ? candidate - middle : middle - candidate;
    if (priority > bestPriority ||
        (priority == bestPriority && priority != 0 &&
         distance < bestDistance)) {
      split = candidate;
      bestPriority = priority;
      bestDistance = distance;
    }
  }

  const std::string left = text.substr(0, byteOffsets[split]);
  const std::string right = text.substr(byteOffsets[split]);
  const std::size_t leftTokens = Encode(left).size();
  const std::size_t rightTokens = Encode(right).size();
  return {
      VoiceTextChunk{left, leftTokens, split},
      VoiceTextChunk{right, rightTokens, codePoints.size() - split},
  };
}

std::vector<std::int64_t> VoiceTextFrontend::EncodePrompt(
    const std::string &referenceText, const std::string &targetText,
    const std::vector<std::int64_t> &referenceCodes) const {
  std::string prompt;
  prompt.reserve(referenceText.size() + targetText.size() +
                 referenceCodes.size() * 8U + 128U);
  prompt.append("[ref_text_start]");
  prompt.append(referenceText);
  prompt.append("[ref_text_end] ");
  prompt.append("[target_text_start]");
  prompt.append(targetText);
  prompt.append("[target_text_end]");
  prompt.append("[ref_speech_start]");
  for (const std::int64_t code : referenceCodes) {
    prompt.push_back('[');
    prompt.append(std::to_string(code));
    prompt.push_back(']');
  }
  prompt.append("[ref_speech_end]");
  prompt.append("[target_speech_start]");
  return Encode(prompt);
}

std::vector<std::int64_t>
VoiceTextFrontend::Encode(const std::string &text) const {
  std::vector<std::int64_t> output;
  std::string ordinary;
  const auto flushOrdinary = [&]() {
    if (ordinary.empty())
      return;
    auto encoded = EncodeOrdinary(ordinary);
    output.insert(output.end(), encoded.begin(), encoded.end());
    ordinary.clear();
  };

  for (std::size_t offset = 0; offset < text.size();) {
    if (text[offset] == '[') {
      const std::size_t end = text.find(']', offset + 1);
      if (end != std::string::npos) {
        const std::string candidate = text.substr(offset, end - offset + 1);
        const auto found = m_vocab.find(candidate);
        if (found != m_vocab.end()) {
          flushOrdinary();
          output.push_back(found->second);
          offset = end + 1;
          continue;
        }
      }
    }
    ordinary.push_back(text[offset++]);
  }
  flushOrdinary();
  return output;
}

std::vector<std::int64_t>
VoiceTextFrontend::EncodeOrdinary(const std::string &text) const {
  const std::string normalized = Normalize(text);
  std::vector<std::int64_t> output;
  for (const std::string &piece : PreTokenize(normalized)) {
    auto encoded = EncodeBpePiece(piece);
    output.insert(output.end(), encoded.begin(), encoded.end());
  }
  return output;
}

std::vector<std::string>
VoiceTextFrontend::PreTokenize(const std::string &text) const {
  std::vector<std::string> pieces;
  std::string current;
  std::optional<PieceClass> currentClass;
  const auto flush = [&]() {
    if (!current.empty())
      pieces.push_back(std::move(current));
    current.clear();
    currentClass.reset();
  };

  for (const CodePoint &point : DecodeUtf8(text)) {
    // The first pre-tokenizer isolates each digit.
    if (IsUnicodeNumber(point.value)) {
      flush();
      pieces.push_back(point.bytes);
      continue;
    }
    const PieceClass valueClass = Classify(point);
    if (currentClass && *currentClass != valueClass)
      flush();
    currentClass = valueClass;
    current.append(point.bytes);
  }
  flush();
  return pieces;
}

std::vector<std::int64_t>
VoiceTextFrontend::EncodeBpePiece(const std::string &piece) const {
  std::vector<std::string> symbols;
  for (const CodePoint &point : DecodeUtf8(piece))
    symbols.push_back(point.bytes);
  if (symbols.empty())
    return {};

  while (symbols.size() > 1) {
    std::size_t bestRank = std::numeric_limits<std::size_t>::max();
    std::string bestLeft;
    std::string bestRight;
    for (std::size_t index = 0; index + 1 < symbols.size(); ++index) {
      const auto found =
          m_mergeRanks.find(PairKey(symbols[index], symbols[index + 1]));
      if (found != m_mergeRanks.end() && found->second < bestRank) {
        bestRank = found->second;
        bestLeft = symbols[index];
        bestRight = symbols[index + 1];
      }
    }
    if (bestRank == std::numeric_limits<std::size_t>::max())
      break;

    std::vector<std::string> merged;
    merged.reserve(symbols.size());
    for (std::size_t index = 0; index < symbols.size();) {
      if (index + 1 < symbols.size() && symbols[index] == bestLeft &&
          symbols[index + 1] == bestRight) {
        merged.push_back(symbols[index] + symbols[index + 1]);
        index += 2;
      } else {
        merged.push_back(std::move(symbols[index]));
        ++index;
      }
    }
    symbols = std::move(merged);
  }

  std::vector<std::int64_t> output;
  output.reserve(symbols.size());
  for (const std::string &symbol : symbols) {
    const auto found = m_vocab.find(symbol);
    output.push_back(found == m_vocab.end() ? m_unknownId : found->second);
  }
  return output;
}

} // namespace videocut::ai::native::detail
