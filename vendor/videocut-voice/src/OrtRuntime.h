#pragma once

#include "videocut/ai/AiTypes.h"

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace videocut::ai::native::detail {

enum class OrtSessionWorkload { Throughput, FramePipeline };
enum class OrtCoreMlModelFormat { MlProgram, NeuralNetwork };

[[nodiscard]] bool OrtBackendAvailable(ComputeBackend backend) noexcept;
// Actual runtime/OS/device environment and all numerical session options.
[[nodiscard]] std::string NativeExecutionEnvironmentIdentity();
[[nodiscard]] std::string OrtExecutionRecipeIdentity(
    ComputeBackend backend, std::uint32_t threads, OrtSessionWorkload workload,
    OrtCoreMlModelFormat format,
    const std::vector<std::pair<std::string, std::int64_t>> &freeDimensions = {});


class OrtModelSession final {
public:
  OrtModelSession(const std::filesystem::path &modelPath,
                  ComputeBackend backend, std::uint32_t threads,
                  OrtSessionWorkload workload = OrtSessionWorkload::Throughput,
                  OrtCoreMlModelFormat coreMlFormat = OrtCoreMlModelFormat::MlProgram,
                  const std::vector<std::pair<std::string, std::int64_t>> &freeDimensions = {},
                  const std::string &verifiedModelDigest = {});

  OrtModelSession(const OrtModelSession &) = delete;
  OrtModelSession &operator=(const OrtModelSession &) = delete;

  [[nodiscard]] Ort::Session &Session() noexcept;
  [[nodiscard]] const std::vector<std::string> &InputNames() const noexcept;
  [[nodiscard]] const std::vector<std::string> &OutputNames() const noexcept;
  [[nodiscard]] const std::vector<const char *> &InputNamePointers() const noexcept;
  [[nodiscard]] const std::vector<const char *> &OutputNamePointers() const noexcept;
  [[nodiscard]] ComputeBackend Backend() const noexcept;
  [[nodiscard]] const std::string &ExecutionRecipeIdentity() const noexcept {
    return m_executionRecipeIdentity;
  }

private:
  ComputeBackend m_backend;
  std::string m_executionRecipeIdentity;
  std::unique_ptr<Ort::Session> m_session;
  std::vector<std::string> m_inputNames;
  std::vector<std::string> m_outputNames;
  std::vector<const char *> m_inputNamePointers;
  std::vector<const char *> m_outputNamePointers;
};

[[nodiscard]] Ort::MemoryInfo CpuMemoryInfo();
[[nodiscard]] std::vector<std::int64_t> TensorShape(const Ort::Value &value);
[[nodiscard]] std::size_t ShapeElementCount(
    const std::vector<std::int64_t> &shape);

} // namespace videocut::ai::native::detail
