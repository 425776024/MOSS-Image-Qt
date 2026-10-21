#pragma once

#include "videocut/ai/model/ModelCatalog.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace videocut::ai::model {

class ResolvedModelPackLease;

struct ResolvedModelFile final {
  std::string role;
  std::filesystem::path path;
  std::uint64_t bytes{0};
  std::string sha256;
};

struct ResolvedModel final {
  ModelSpec spec;
  std::vector<ResolvedModelFile> files;

  // A protected Bundle source keeps any small transient plaintext support
  // files alive through this lease. Signed-direct weights and local
  // development sources do not need one.
  std::shared_ptr<const ResolvedModelPackLease> packLease;

  [[nodiscard]] const ResolvedModelFile *
  FileForRole(const std::string &role) const noexcept;
};

enum class ModelAvailabilityState : std::uint8_t {
  Available = 0,
  Missing,
  Corrupt,
};

// Small license documents from the source's admitted model-pack inventory.
// Full text is requested explicitly and never resolves inference payloads.
struct ModelLicenseDocument final {
  std::string name;
  std::string sha256;
  std::string text;
};

struct ModelAvailability final {
  ModelSpec spec;
  ModelAvailabilityState state{ModelAvailabilityState::Missing};
  AiError error;
};

/// Immutable source of model identities and session-scoped resolved files.
///
/// A source may expose development files, signed-direct Bundle weights, or
/// decrypt small Bundle objects into a transient lease. It never downloads a
/// model.
class IModelSource {
public:
  virtual ~IModelSource() = default;

  [[nodiscard]] virtual Result<ResolvedModel>
  Resolve(const ModelRequest &request) const = 0;
  [[nodiscard]] virtual std::vector<ModelAvailability> InspectAll() const = 0;
  [[nodiscard]] virtual Result<std::vector<ModelLicenseDocument>>
  ReadLicenses(const ModelRequest &exact) const = 0;
  [[nodiscard]] virtual const ModelCatalog &Catalog() const noexcept = 0;
};

[[nodiscard]] const char *
ModelAvailabilityStateName(ModelAvailabilityState value) noexcept;

} // namespace videocut::ai::model
