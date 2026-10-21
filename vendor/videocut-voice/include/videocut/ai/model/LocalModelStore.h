#pragma once

#include "videocut/ai/model/ModelSource.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace videocut::ai::model {

/// Resolves only raw, immutable model files below one project-local root.
/// It is a Debug/development source and deliberately performs no decryption,
/// temporary materialization, or network access.
class LocalModelStore final : public IModelSource {
public:
  [[nodiscard]] static Result<std::shared_ptr<LocalModelStore>>
  Open(std::filesystem::path modelRoot, ModelCatalog catalog);

  /// Opens a build-staged Debug store whose receipt closes the complete
  /// catalog and immutable file inventory. InspectAll uses only that verified
  /// receipt plus no-follow size, timestamp and filesystem identity checks.
  /// Resolve reuses that build-verified digest while this identity is intact;
  /// non-staged development stores still perform first-use SHA-256 verification.
  /// The Debug receipt is trusted build output, not a signed security boundary.
  /// Distribution model packs use their separate authenticated source.
  [[nodiscard]] static Result<std::shared_ptr<LocalModelStore>>
  OpenStagedImmutable(std::filesystem::path modelRoot, ModelCatalog catalog,
                      std::filesystem::path receiptPath);

  [[nodiscard]] Result<ResolvedModel>
  Resolve(const ModelRequest &request) const override;
  [[nodiscard]] std::vector<ModelAvailability> InspectAll() const override;
  [[nodiscard]] Result<std::vector<ModelLicenseDocument>>
  ReadLicenses(const ModelRequest &exact) const override;
  [[nodiscard]] const std::filesystem::path &Root() const noexcept;
  [[nodiscard]] const ModelCatalog &Catalog() const noexcept override;

private:
  struct VerifiedFileStamp final {
    std::uint64_t bytes{0};
    std::int64_t modifiedTicks{0};
    std::string sha256;
  };

  struct StagedFileReceipt final {
    std::filesystem::path relativePath;
    std::uint64_t bytes{0};
    std::string sha256;
    std::int64_t modifiedSeconds{0};
    std::int64_t modifiedNanoseconds{0};
    std::uint64_t device{0}, inode{0};
    std::int64_t changedSeconds{0}, changedNanoseconds{0};
    [[nodiscard]] bool matchesFile(const std::filesystem::path &path) const noexcept;
  };

  LocalModelStore(
      std::filesystem::path modelRoot, ModelCatalog catalog,
      std::unordered_map<std::string, StagedFileReceipt> stagedFiles = {});

  [[nodiscard]] Result<ResolvedModelFile>
  ResolveFile(const ModelFileSpec &file) const;
  [[nodiscard]] AiError InspectStagedFile(const ModelSpec &model,
                                          const ModelFileSpec &file) const;

  std::filesystem::path m_modelRoot;
  ModelCatalog m_catalog;
  // Exact license arrays retained from the already admitted v1 pack receipts.
  std::unordered_map<std::string, std::string> m_licenseInventories;
  std::unordered_map<std::string, StagedFileReceipt> m_stagedFiles;
  mutable std::mutex m_verificationMutex;
  mutable std::unordered_map<std::string, VerifiedFileStamp> m_verifiedFiles;
};

} // namespace videocut::ai::model
