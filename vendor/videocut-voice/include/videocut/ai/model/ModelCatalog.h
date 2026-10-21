#pragma once

#include "videocut/ai/AiTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace videocut::ai::model {

struct ModelFileSpec final {
  std::string role;
  std::filesystem::path relativePath;
  std::uint64_t bytes{0};
  std::string sha256;
};

/// Exact immutable metadata retained for the target ModelPack library and
/// scheduler admission path. Legacy schema-1 catalogs intentionally leave
/// this absent; their current runtime selection semantics are unchanged.
struct ModelPackAdmissionMetadata final {
  std::string packId;
  std::string packDigest;
  std::string sourceIdentity;
  std::string licenseIdentity;
  std::string providerIdentity;
  std::vector<std::string> requiredDeviceFeatures;
  std::string inputSchemaDigest;
  std::string outputSchemaDigest;
  std::string recipeCompatibilityDigest;
};

struct ModelSpec final {
  std::string modelId;
  std::string version;
  std::string digest;
  AiTask task{AiTask::Vad};
  std::string runtimeId;
  std::string variant;
  bool isDefault{false};
  SessionResidency residency{SessionResidency::OnDemand};
  std::uint32_t maximumBatchSize{1};
  std::uint64_t estimatedResidentBytes{0};
  std::vector<ComputeBackend> preferredBackends;
  std::vector<ModelFileSpec> files;
  std::optional<ModelPackAdmissionMetadata> admissionMetadata;
};

struct ModelRequest final {
  std::string modelId;
  std::optional<std::string> exactVersion;
};

class ModelCatalog final {
public:
  [[nodiscard]] static Result<ModelCatalog>
  LoadFromFile(const std::filesystem::path &path);
  [[nodiscard]] static Result<ModelCatalog>
  FromModels(std::vector<ModelSpec> models);

  [[nodiscard]] Result<ModelSpec> Find(const ModelRequest &request) const;
  [[nodiscard]] const std::vector<ModelSpec> &Models() const noexcept;

private:
  std::vector<ModelSpec> m_models;
};

} // namespace videocut::ai::model
