#pragma once

#include "videocut/ai/Inference.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace videocut::ai::native {

/// One monotonic alignment segment between the input and normalized Unicode
/// code-point sequences. A rewrite maps its output to the source it consumed.
struct TextNormalizationSpan final {
  std::size_t inputBegin{0};
  std::size_t inputEnd{0};
  std::size_t outputBegin{0};
  std::size_t outputEnd{0};
};

struct TextNormalizationResult final {
  std::string text;
  std::vector<TextNormalizationSpan> spans;
};

enum class TextNormalizationDirection {
  WrittenToSpoken,
  SpokenToWritten,
};

/// Immutable, reusable OpenFst text-normalization pipeline. Rules are applied
/// in order and loaded once. Instances can be shared by ASR, subtitles,
/// imports, and voice-synthesis frontends.
class FstTextNormalizer final {
public:
  ~FstTextNormalizer();

  FstTextNormalizer(const FstTextNormalizer &) = delete;
  FstTextNormalizer &operator=(const FstTextNormalizer &) = delete;

  [[nodiscard]] static Result<std::shared_ptr<FstTextNormalizer>>
  Load(const std::vector<std::filesystem::path> &ruleFsts);

  /// Loads a WeTextProcessing-compatible tagger/parser/verbalizer pipeline.
  /// Structured tagger output is reordered before verbalizer composition.
  [[nodiscard]] static Result<std::shared_ptr<FstTextNormalizer>>
  LoadStructured(
      const std::filesystem::path &taggerFst,
      const std::filesystem::path &verbalizerFst,
      TextNormalizationDirection direction,
      const std::vector<std::filesystem::path> &trailingRuleFsts = {});

  [[nodiscard]] Result<TextNormalizationResult>
  Normalize(const std::string &text) const;

private:
  struct Impl;
  explicit FstTextNormalizer(std::unique_ptr<Impl> implementation);

  std::unique_ptr<Impl> m_implementation;
};

/// Convenience entry point for one-shot tools. Runtime code should retain the
/// loaded FstTextNormalizer to avoid reparsing rule files.
[[nodiscard]] Result<TextNormalizationResult>
NormalizeTextWithFsts(const std::vector<std::filesystem::path> &ruleFsts,
                      const std::string &text);

} // namespace videocut::ai::native
