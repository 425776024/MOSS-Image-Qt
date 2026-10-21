#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace videocut::ai::native::detail {

struct VoiceTextChunk final {
  std::string text;
  std::size_t tokenCount{0};
  std::size_t codePointCount{0};
};

/// Pure C++ evaluator for the locked voice tokenizer.json. It implements the
/// exact Lowercase/whitespace normalizer, Digits/Split pre-tokenizers, added
/// token extraction, and BPE merge table used by the model.
class VoiceTextFrontend final {
public:
  explicit VoiceTextFrontend(const std::filesystem::path &tokenizerJson);

  [[nodiscard]] std::vector<std::int64_t> Encode(const std::string &text) const;
  [[nodiscard]] std::vector<std::int64_t>
  EncodePrompt(const std::string &referenceText, const std::string &targetText,
               const std::vector<std::int64_t> &referenceCodes) const;
  [[nodiscard]] std::int64_t IdForToken(const std::string &token) const;
  [[nodiscard]] std::optional<std::int64_t>
  AudioCodeForId(std::int64_t tokenId) const;
  [[nodiscard]] std::int64_t SpeechEndId() const noexcept;
  [[nodiscard]] std::size_t VocabularySize() const noexcept;
  /// Splits target text only when its exact locked-tokenizer representation
  /// exceeds [maximumTargetTokens]. Concatenating the returned UTF-8 slices
  /// always reconstructs [text] byte-for-byte.
  [[nodiscard]] std::vector<VoiceTextChunk>
  PlanTargetChunks(const std::string &text,
                   std::size_t maximumTargetTokens) const;
  [[nodiscard]] std::pair<VoiceTextChunk, VoiceTextChunk>
  BisectTargetChunk(const std::string &text) const;

private:
  [[nodiscard]] std::vector<std::int64_t>
  EncodeOrdinary(const std::string &text) const;
  [[nodiscard]] std::vector<std::string>
  PreTokenize(const std::string &text) const;
  [[nodiscard]] std::vector<std::int64_t>
  EncodeBpePiece(const std::string &piece) const;

  std::unordered_map<std::string, std::int64_t> m_vocab;
  std::unordered_map<std::string, std::size_t> m_mergeRanks;
  std::vector<std::int64_t> m_audioCodeByTokenId;
  std::int64_t m_unknownId{0};
  std::int64_t m_speechEndId{-1};
  std::size_t m_vocabularySize{0};
};

} // namespace videocut::ai::native::detail
