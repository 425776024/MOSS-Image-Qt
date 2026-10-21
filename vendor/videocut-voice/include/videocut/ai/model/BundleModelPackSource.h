#pragma once

#include "videocut/ai/model/ModelSource.h"

#include <filesystem>
#include <memory>

namespace videocut::ai::model {

/// Owns the small private plaintext materialization used by encrypted support
/// objects. Destruction removes every materialized file and its directory.
/// Large signed-direct weights need no lease or temporary copy.
class ResolvedModelPackLease final {
public:
  ~ResolvedModelPackLease();

  ResolvedModelPackLease(const ResolvedModelPackLease &) = delete;
  ResolvedModelPackLease &operator=(const ResolvedModelPackLease &) = delete;

private:
  struct Impl;
  explicit ResolvedModelPackLease(std::unique_ptr<Impl> implementation);

  std::unique_ptr<Impl> m_implementation;

  friend class BundleModelPackSource;
};

/// Resolves schema-3 hybrid model packs from the signed read-only app Bundle.
/// Small objects retain authenticated AES-256-GCM chunks; large weights use a
/// neutral content-addressed signed-direct object so native runtimes can mmap
/// them without a multi-gigabyte decrypt/copy step. This API is linked only by
/// protected Release/Distribution targets; Debug deliberately has no Bundle
/// resolver. Direct objects rely on the caller supplying the code-signed,
/// read-only App resource root: the build gate hashes every byte before
/// signing, while runtime resolution deliberately avoids repeating a full
/// multi-gigabyte hash pass.
class BundleModelPackSource final : public IModelSource {
public:
  struct Impl;

  [[nodiscard]] static Result<std::shared_ptr<BundleModelPackSource>>
  Open(std::filesystem::path bundlePackRoot);

  ~BundleModelPackSource() override;
  BundleModelPackSource(const BundleModelPackSource &) = delete;
  BundleModelPackSource &operator=(const BundleModelPackSource &) = delete;

  [[nodiscard]] Result<ResolvedModel>
  Resolve(const ModelRequest &request) const override;
  [[nodiscard]] std::vector<ModelAvailability> InspectAll() const override;
  [[nodiscard]] Result<std::vector<ModelLicenseDocument>>
  ReadLicenses(const ModelRequest &exact) const override;
  [[nodiscard]] const ModelCatalog &Catalog() const noexcept override;
  [[nodiscard]] const std::filesystem::path &Root() const noexcept;

private:
  explicit BundleModelPackSource(std::unique_ptr<Impl> implementation);

  std::unique_ptr<Impl> m_implementation;
};

} // namespace videocut::ai::model
