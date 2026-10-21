#include "videocut/ai/native/TextNormalization.h"

#include <fst/fstlib.h>
#include <kaldifst/csrc/text-normalizer.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace videocut::ai::native {
namespace {

constexpr std::size_t kMaximumAlignmentCells = 4U * 1024U * 1024U;
using FieldOrders =
    std::unordered_map<std::string, std::vector<std::string>>;

std::vector<std::string> Utf8Codepoints(const std::string &text) {
  std::vector<std::string> result;
  for (std::size_t offset = 0; offset < text.size();) {
    const auto lead = static_cast<std::uint8_t>(text[offset]);
    std::size_t width = 0U;
    if (lead <= 0x7FU) width = 1U;
    else if ((lead & 0xE0U) == 0xC0U) width = 2U;
    else if ((lead & 0xF0U) == 0xE0U) width = 3U;
    else if ((lead & 0xF8U) == 0xF0U) width = 4U;
    else throw std::invalid_argument("text normalization input is not UTF-8");
    if (width > text.size() - offset)
      throw std::invalid_argument(
          "text normalization input is truncated UTF-8");
    for (std::size_t index = 1U; index < width; ++index) {
      if ((static_cast<std::uint8_t>(text[offset + index]) & 0xC0U) != 0x80U)
        throw std::invalid_argument("text normalization input is not UTF-8");
    }
    result.push_back(text.substr(offset, width));
    offset += width;
  }
  return result;
}

// Structured token ordering follows WeTextProcessing's Apache-2.0 runtime at
// tag WeTextProcessing (327efb8). This parser throws instead of using glog
// fatal checks, so malformed rule output cannot terminate the host process.
const std::set<std::string> kAsciiTokenCharacters{
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l",
    "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
    "y", "z", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z", "_"};
const FieldOrders kChineseTnOrders{
    {"date", {"year", "month", "day"}},
    {"fraction", {"denominator", "numerator"}},
    {"measure", {"denominator", "numerator", "value"}},
    {"money", {"value", "currency"}},
    {"time", {"noon", "hour", "minute", "second"}}};
const FieldOrders kChineseItnOrders{
    {"date", {"year", "month", "day", "preserve_order"}},
    {"fraction", {"sign", "numerator", "denominator"}},
    {"measure", {"numerator", "denominator", "value", "units"}},
    {"money", {"currency", "value", "decimal", "quantity"}},
    {"time", {"hour", "minute", "second", "noon", "zone"}},
    {"telephone", {"country_code", "number_part"}},
    {"electronic", {"username", "domain", "protocol"}}};

class StructuredTokenParser final {
public:
  explicit StructuredTokenParser(const TextNormalizationDirection direction)
      : m_orders(direction == TextNormalizationDirection::WrittenToSpoken
                     ? kChineseTnOrders
                     : kChineseItnOrders) {}

  std::string Reorder(const std::string &input) const {
    const auto characters = Utf8Codepoints(input);
    if (characters.empty())
      throw std::invalid_argument(
          "structured normalization token stream is empty");
    std::size_t cursor = 0U;
    std::string output;
    const auto skipSpaces = [&]() {
      while (cursor < characters.size() && characters[cursor] == " ")
        ++cursor;
    };
    const auto consume = [&](const std::string &expected) {
      const auto expectedCharacters = Utf8Codepoints(expected);
      if (cursor + expectedCharacters.size() > characters.size() ||
          !std::equal(expectedCharacters.begin(), expectedCharacters.end(),
                      characters.begin() +
                          static_cast<std::ptrdiff_t>(cursor)))
        throw std::invalid_argument(
            "structured normalization token delimiter is malformed");
      cursor += expectedCharacters.size();
    };
    const auto readKey = [&]() {
      std::string key;
      while (cursor < characters.size() &&
             kAsciiTokenCharacters.count(characters[cursor]) != 0U)
        key += characters[cursor++];
      if (key.empty())
        throw std::invalid_argument(
            "structured normalization token key is malformed");
      return key;
    };

    while (cursor < characters.size()) {
      skipSpaces();
      if (cursor == characters.size()) break;
      const std::string name = readKey();
      consume(" { ");
      std::vector<std::string> order;
      std::unordered_map<std::string, std::string> values;
      bool closed = false;
      while (cursor < characters.size()) {
        skipSpaces();
        if (cursor < characters.size() && characters[cursor] == "}") {
          ++cursor;
          closed = true;
          break;
        }
        const std::string key = readKey();
        consume(": \"");
        std::string value;
        while (cursor < characters.size() && characters[cursor] != "\"") {
          value += characters[cursor];
          if (characters[cursor++] == "\\") {
            if (cursor >= characters.size())
              throw std::invalid_argument(
                  "structured normalization token escape is truncated");
            value += characters[cursor++];
          }
        }
        consume("\"");
        order.push_back(key);
        values[key] = std::move(value);
      }
      if (!closed)
        throw std::invalid_argument(
            "structured normalization token is unterminated");
      const auto configured = m_orders.find(name);
      if (configured != m_orders.end() &&
          (values.count("preserve_order") == 0U ||
           values.at("preserve_order") != "true"))
        order = configured->second;
      if (!output.empty()) output.push_back(' ');
      output += name + " {";
      for (const auto &key : order) {
        const auto value = values.find(key);
        if (value != values.end())
          output += " " + key + ": \"" + value->second + "\"";
      }
      output += " }";
    }
    return output;
  }

private:
  FieldOrders m_orders;
};

class StructuredFstPipeline final {
public:
  StructuredFstPipeline(const std::filesystem::path &tagger,
                        const std::filesystem::path &verbalizer,
                        const TextNormalizationDirection direction)
      : m_tagger(fst::StdVectorFst::Read(tagger.string())),
        m_verbalizer(fst::StdVectorFst::Read(verbalizer.string())),
        m_parser(direction) {
    if (!m_tagger || !m_verbalizer)
      throw std::runtime_error(
          "unable to read structured text-normalization FSTs");
  }

  std::string Normalize(const std::string &input) const {
    if (input.empty()) return {};
    const std::string tagged = Compose(input, *m_tagger);
    const std::string reordered = m_parser.Reorder(tagged);
    std::string output = Compose(reordered, *m_verbalizer);
    output.erase(std::remove(output.begin(), output.end(), '\0'), output.end());
    return output;
  }

private:
  static std::string Compose(const std::string &input,
                             const fst::StdVectorFst &rule) {
    fst::StringCompiler<fst::StdArc> compiler;
    fst::StdVectorFst inputFst;
    if (!compiler(input, &inputFst))
      throw std::runtime_error(
          "unable to compile structured normalization input");
    fst::StdVectorFst lattice;
    fst::Compose(inputFst, rule, &lattice);
    if (lattice.Start() == fst::kNoStateId)
      throw std::runtime_error(
          "structured normalization rule rejected its input");
    fst::StdVectorFst shortest;
    fst::ShortestPath(lattice, &shortest, 1U, true);
    fst::StringPrinter<fst::StdArc> printer;
    std::string output;
    if (!printer(shortest, &output))
      throw std::runtime_error(
          "unable to print structured normalization output");
    return output;
  }

  std::unique_ptr<fst::StdVectorFst> m_tagger;
  std::unique_ptr<fst::StdVectorFst> m_verbalizer;
  StructuredTokenParser m_parser;
};

std::vector<TextNormalizationSpan>
FallbackAlignment(const std::size_t inputSize, const std::size_t outputSize) {
  if (inputSize == 0U && outputSize == 0U) return {};
  return {{0U, inputSize, 0U, outputSize}};
}

std::vector<TextNormalizationSpan>
Align(const std::vector<std::string> &input,
      const std::vector<std::string> &output) {
  if (input == output) {
    if (input.empty()) return {};
    return {{0U, input.size(), 0U, output.size()}};
  }
  if (input.empty() || output.empty() ||
      input.size() > kMaximumAlignmentCells / output.size())
    return FallbackAlignment(input.size(), output.size());
  const std::size_t columns = output.size() + 1U;
  std::vector<std::uint32_t> lcs((input.size() + 1U) * columns, 0U);
  for (std::size_t i = input.size(); i-- > 0U;) {
    for (std::size_t j = output.size(); j-- > 0U;) {
      const std::size_t cell = i * columns + j;
      lcs[cell] = input[i] == output[j]
                      ? 1U + lcs[(i + 1U) * columns + j + 1U]
                      : std::max(lcs[(i + 1U) * columns + j],
                                 lcs[i * columns + j + 1U]);
    }
  }
  std::vector<TextNormalizationSpan> spans;
  std::size_t inputCursor = 0U;
  std::size_t outputCursor = 0U;
  std::size_t changedInputBegin = 0U;
  std::size_t changedOutputBegin = 0U;
  bool hasChange = false;
  const auto flushChange = [&]() {
    if (!hasChange) return;
    spans.push_back({changedInputBegin, inputCursor, changedOutputBegin,
                     outputCursor});
    hasChange = false;
  };
  while (inputCursor < input.size() && outputCursor < output.size()) {
    if (input[inputCursor] == output[outputCursor]) {
      flushChange();
      spans.push_back({inputCursor, inputCursor + 1U, outputCursor,
                       outputCursor + 1U});
      ++inputCursor;
      ++outputCursor;
    } else {
      if (!hasChange) {
        changedInputBegin = inputCursor;
        changedOutputBegin = outputCursor;
        hasChange = true;
      }
      if (lcs[(inputCursor + 1U) * columns + outputCursor] >=
          lcs[inputCursor * columns + outputCursor + 1U])
        ++inputCursor;
      else
        ++outputCursor;
    }
  }
  if (inputCursor < input.size() || outputCursor < output.size()) {
    if (!hasChange) {
      changedInputBegin = inputCursor;
      changedOutputBegin = outputCursor;
      hasChange = true;
    }
    inputCursor = input.size();
    outputCursor = output.size();
  }
  flushChange();
  return spans;
}

Result<std::string> ValidateRuleFile(const std::filesystem::path &path) {
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes == 0U || path.extension() != ".fst")
    return Result<std::string>::Failure(
        AiErrorCode::ModelFileCorrupt,
        "text normalization rule is missing, empty, or not an .fst: " +
            path.string());
  return Result<std::string>::Success(path.string());
}

} // namespace

struct FstTextNormalizer::Impl final {
  explicit Impl(const std::vector<std::filesystem::path> &paths) {
    rules.reserve(paths.size());
    for (const auto &path : paths)
      rules.push_back(std::make_unique<kaldifst::TextNormalizer>(path.string()));
  }
  Impl(const std::filesystem::path &tagger,
       const std::filesystem::path &verbalizer,
       const TextNormalizationDirection direction,
       const std::vector<std::filesystem::path> &paths)
      : structured(std::make_unique<StructuredFstPipeline>(tagger, verbalizer,
                                                           direction)) {
    rules.reserve(paths.size());
    for (const auto &path : paths)
      rules.push_back(std::make_unique<kaldifst::TextNormalizer>(path.string()));
  }
  std::unique_ptr<StructuredFstPipeline> structured;
  std::vector<std::unique_ptr<kaldifst::TextNormalizer>> rules;
  mutable std::mutex mutex;
};

FstTextNormalizer::FstTextNormalizer(std::unique_ptr<Impl> implementation)
    : m_implementation(std::move(implementation)) {}
FstTextNormalizer::~FstTextNormalizer() = default;

Result<std::shared_ptr<FstTextNormalizer>>
FstTextNormalizer::Load(
    const std::vector<std::filesystem::path> &ruleFsts) {
  if (ruleFsts.empty())
    return Result<std::shared_ptr<FstTextNormalizer>>::Failure(
        AiErrorCode::InvalidArgument,
        "text normalization requires at least one rule FST");
  try {
    for (const auto &path : ruleFsts) {
      auto valid = ValidateRuleFile(path);
      if (!valid.ok())
        return Result<std::shared_ptr<FstTextNormalizer>>::Failure(
            valid.error());
    }
    return Result<std::shared_ptr<FstTextNormalizer>>::Success(
        std::shared_ptr<FstTextNormalizer>(
            new FstTextNormalizer(std::make_unique<Impl>(ruleFsts))));
  } catch (const std::exception &exception) {
    return Result<std::shared_ptr<FstTextNormalizer>>::Failure(
        AiErrorCode::ModelFileCorrupt,
        std::string("unable to load text normalization FST: ") +
            exception.what());
  }
}

Result<std::shared_ptr<FstTextNormalizer>>
FstTextNormalizer::LoadStructured(
    const std::filesystem::path &taggerFst,
    const std::filesystem::path &verbalizerFst,
    const TextNormalizationDirection direction,
    const std::vector<std::filesystem::path> &trailingRuleFsts) {
  try {
    std::vector<std::filesystem::path> paths{taggerFst, verbalizerFst};
    paths.insert(paths.end(), trailingRuleFsts.begin(),
                 trailingRuleFsts.end());
    for (const auto &path : paths) {
      auto valid = ValidateRuleFile(path);
      if (!valid.ok())
        return Result<std::shared_ptr<FstTextNormalizer>>::Failure(
            valid.error());
    }
    return Result<std::shared_ptr<FstTextNormalizer>>::Success(
        std::shared_ptr<FstTextNormalizer>(new FstTextNormalizer(
            std::make_unique<Impl>(taggerFst, verbalizerFst, direction,
                                   trailingRuleFsts))));
  } catch (const std::exception &exception) {
    return Result<std::shared_ptr<FstTextNormalizer>>::Failure(
        AiErrorCode::ModelFileCorrupt,
        std::string("unable to load structured normalization FSTs: ") +
            exception.what());
  }
}

Result<TextNormalizationResult>
FstTextNormalizer::Normalize(const std::string &text) const {
  try {
    const auto inputCodepoints = Utf8Codepoints(text);
    std::string normalized = text;
    {
      std::lock_guard<std::mutex> lock(m_implementation->mutex);
      if (m_implementation->structured)
        normalized = m_implementation->structured->Normalize(normalized);
      for (const auto &rule : m_implementation->rules)
        normalized = rule->Normalize(normalized);
    }
    const auto outputCodepoints = Utf8Codepoints(normalized);
    return Result<TextNormalizationResult>::Success(
        {std::move(normalized), Align(inputCodepoints, outputCodepoints)});
  } catch (const std::invalid_argument &exception) {
    return Result<TextNormalizationResult>::Failure(AiErrorCode::InvalidArgument,
                                                     exception.what());
  } catch (const std::exception &exception) {
    return Result<TextNormalizationResult>::Failure(
        AiErrorCode::InferenceFailed,
        std::string("text normalization failed: ") + exception.what());
  }
}

Result<TextNormalizationResult>
NormalizeTextWithFsts(
    const std::vector<std::filesystem::path> &ruleFsts,
    const std::string &text) {
  auto normalizer = FstTextNormalizer::Load(ruleFsts);
  if (!normalizer.ok())
    return Result<TextNormalizationResult>::Failure(normalizer.error());
  return normalizer.value()->Normalize(text);
}

} // namespace videocut::ai::native
