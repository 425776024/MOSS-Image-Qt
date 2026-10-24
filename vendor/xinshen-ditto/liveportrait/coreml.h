#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lp {
// Reject Core ML plans that assign inference operations to CPU. This is a
// placement check, not a claim of measured hardware utilization.
void validateCoreMLAcceleration(const std::filesystem::path& cache, const std::string& component);
// Native MLProgram path: the bundled ORT bridge rejects fp16 GPU operators.
class CoreMLGenerator {
 public:
  class Prepared;
  using PreparedPtr = std::shared_ptr<Prepared>;
  using InputRefs = std::vector<const std::vector<float>*>;
  static std::unique_ptr<CoreMLGenerator> create(const std::filesystem::path& source);
  ~CoreMLGenerator();
  bool split() const;
  const std::string& profile() const;
  const std::string& manifestSha256() const;
  // A fixed anchor makes the same frame independent of segment/seek order.
  int64_t maskAnchor(int64_t frameIndex) const;
  // For split profiles, one producer owns prepare() and one consumer owns
  // finish(). Their distinct CoreML models and counters may run concurrently.
  // Prepared retains the feature provider until the decoder has consumed it.
  PreparedPtr prepare(const std::vector<std::string>& names,
                      const std::vector<std::vector<float>>& data,
                      const std::vector<std::vector<int64_t>>& shapes,
                      int64_t frameIndex = -1,
                      const std::vector<float>& maskDriving = {});
  // Borrowed inputs must remain unchanged until this synchronous call returns.
  // Prepared retains CoreML-owned output tensors, never these C++ references.
  PreparedPtr prepareRefs(const std::vector<std::string>& names,
                          const InputRefs& data,
                          const std::vector<std::vector<int64_t>>& shapes,
                          int64_t frameIndex = -1,
                          const std::vector<float>& maskDriving = {});
  std::vector<float> finish(PreparedPtr prepared);
  std::vector<float> run(const std::vector<std::string>& names,
                         const std::vector<std::vector<float>>& data,
                         const std::vector<std::vector<int64_t>>& shapes);
 private:
  struct State;
  explicit CoreMLGenerator(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
}
