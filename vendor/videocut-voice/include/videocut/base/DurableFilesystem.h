#pragma once

#include "BaseApi.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace videocut::base {

/// Stable failure categories for durable filesystem operations. Native error
/// numbers are diagnostic-only and must not be used for product decisions.
enum class DurableFilesystemErrorCode : std::uint8_t {
  InvalidArgument = 0,
  NotFound = 1,
  AlreadyExists = 2,
  SymlinkRejected = 3,
  NotRegularFile = 4,
  NotDirectory = 5,
  EntryChanged = 6,
  LimitExceeded = 7,
  CrossDevice = 8,
  OperationNotSupported = 9,
  IoFailure = 10,
  PermissionDenied = 11,
  ResourceExhausted = 12,
};

enum class DurableFilesystemOperation : std::uint8_t {
  ValidateArgument = 0,
  InspectEntry = 1,
  OpenEntry = 2,
  CreateDirectory = 3,
  ReadFile = 4,
  WriteFile = 5,
  SyncFile = 6,
  SyncDirectory = 7,
  EnumerateDirectory = 8,
  RenameEntry = 9,
  RemoveEntry = 10,
  QuarantineEntry = 11,
  CloseEntry = 12,
};

struct DurableFilesystemError final {
  DurableFilesystemErrorCode code{DurableFilesystemErrorCode::IoFailure};
  DurableFilesystemOperation operation{
      DurableFilesystemOperation::ValidateArgument};
  int nativeError{0};
};

class VIDEOCUT_BASE_API DurableDirectoryLock final {
public:
  ~DurableDirectoryLock();
  DurableDirectoryLock(const DurableDirectoryLock &) = delete;
  DurableDirectoryLock &operator=(const DurableDirectoryLock &) = delete;
  DurableDirectoryLock(DurableDirectoryLock &&) noexcept;
  DurableDirectoryLock &operator=(DurableDirectoryLock &&) noexcept;

private:
  struct Impl;
  explicit DurableDirectoryLock(std::unique_ptr<Impl> impl) noexcept;
  friend std::variant<std::unique_ptr<DurableDirectoryLock>,
                      DurableFilesystemError>
  AcquireDurableDirectoryLock(const std::filesystem::path &,
                              std::string_view) noexcept;
  std::unique_ptr<Impl> m_impl;
};

using DurableDirectoryLockResult =
    std::variant<std::unique_ptr<DurableDirectoryLock>,
                 DurableFilesystemError>;

/// Acquires an exclusive cross-process advisory lease backed by one regular,
/// no-follow child file of directory. The lease is released by destruction;
/// the lock file itself remains as a stable rendezvous object.
VIDEOCUT_BASE_API DurableDirectoryLockResult AcquireDurableDirectoryLock(
    const std::filesystem::path &directory,
    std::string_view lockFileName) noexcept;

VIDEOCUT_BASE_API const char *
DurableFilesystemErrorCodeName(DurableFilesystemErrorCode code) noexcept;
VIDEOCUT_BASE_API const char *
DurableFilesystemOperationName(DurableFilesystemOperation operation) noexcept;
VIDEOCUT_BASE_API std::string
DescribeDurableFilesystemError(const DurableFilesystemError &error);

/// Bounds every recursive traversal. The root child counts as one entry and
/// has depth zero. A zero bound is invalid and fails before touching disk.
struct DurableTreeLimits final {
  std::uint64_t maximumEntries{8192U};
  std::uint32_t maximumDepth{64U};
};

enum class DurableDirectoryDisposition : std::uint8_t {
  Created = 0,
  AlreadyExisted = 1,
};

using DurableDirectoryResult =
    std::variant<DurableDirectoryDisposition, DurableFilesystemError>;

/// Ensures one direct child directory exists beneath an already-existing real
/// parent. parent is a host-trusted authority and must be absolute, normalized,
/// and non-root. childName must be one host-generated portable path component;
/// separators, roots, ".", "..", and embedded NUL bytes are rejected. A newly
/// created directory and its parent are synced before Created is returned.
VIDEOCUT_BASE_API DurableDirectoryResult EnsureDurableChildDirectory(
    const std::filesystem::path &parent, std::string_view childName) noexcept;

using DurableReadResult = std::variant<std::string, DurableFilesystemError>;

struct DurableFileFingerprint final {
  std::string contentDigest;
  std::uint64_t byteLength{0U};
};

using DurableFileFingerprintResult =
    std::variant<DurableFileFingerprint, DurableFilesystemError>;

using DurableFileSizeResult =
    std::variant<std::uint64_t, DurableFilesystemError>;

/// Streams one regular file through a no-follow handle and returns the SHA-256
/// digest of every byte. The operation fails if the bound file identity, size,
/// or timestamps change while it is read.
VIDEOCUT_BASE_API DurableFileFingerprintResult
FingerprintRegularFileNoFollow(const std::filesystem::path &path) noexcept;

/// Bounded variant used for untrusted cache artifacts. The file size is
/// rejected before hashing and the streaming loop independently enforces the
/// same ceiling, so a concurrently growing file cannot consume unbounded I/O.
VIDEOCUT_BASE_API DurableFileFingerprintResult
FingerprintRegularFileNoFollowBounded(const std::filesystem::path &path,
                                      std::uint64_t maximumBytes) noexcept;

/// Opens one direct regular-file child relative to a pinned no-follow parent
/// and returns the descriptor-bound byte length without reading the payload.
/// This is intended for quota accounting where hashing every immutable cache
/// object during each maintenance pass would make admission O(total bytes).
VIDEOCUT_BASE_API DurableFileSizeResult
InspectRegularFileChildSizeNoFollow(const std::filesystem::path &parent,
                                    std::string_view childName) noexcept;

/// Reads one regular file through a no-follow handle. The operation fails if
/// the file is larger than maximumBytes or its identity/size/timestamps change
/// while it is read.
VIDEOCUT_BASE_API DurableReadResult ReadRegularFileNoFollow(
    const std::filesystem::path &path, std::uint64_t maximumBytes) noexcept;

/// Reads one direct regular-file child relative to a no-follow parent
/// authority. Both the parent directory and child identity remain bound while
/// bytes are read, so replacing the parent leaf with a symlink cannot redirect
/// the operation. Platforms without descriptor-relative child access fail
/// closed with OperationNotSupported.
VIDEOCUT_BASE_API DurableReadResult ReadRegularFileChildNoFollow(
    const std::filesystem::path &parent, std::string_view childName,
    std::uint64_t maximumBytes) noexcept;

/// Reads one regular-file descendant through a pinned root directory and a
/// descriptor-relative no-follow walk. Every directory component and the leaf
/// must be one portable child name. An ancestor rename cannot redirect an
/// already-open walk; symlinks and identity changes fail closed. Platforms
/// without descriptor-relative descendant access return OperationNotSupported.
VIDEOCUT_BASE_API DurableReadResult ReadRegularFileDescendantNoFollow(
    const std::filesystem::path &root,
    const std::vector<std::string> &directoryComponents,
    std::string_view childName, std::uint64_t maximumBytes) noexcept;

/// Reads one regular-file descendant whose exact local filesystem components
/// are bounded valid UTF-8. Unlike ReadRegularFileDescendantNoFollow, this
/// deliberately does not require portable ASCII names and must not be used to
/// authorize portable artifact identities or writes. Components are never
/// Unicode-normalized: NFC and NFD names remain distinct exact identities.
/// The pinned-root descriptor walk, no-follow, regular-file, size, and
/// identity-change guarantees are otherwise identical to the portable API.
VIDEOCUT_BASE_API DurableReadResult ReadLocalRegularFileDescendantNoFollow(
    const std::filesystem::path &root,
    const std::vector<std::string> &directoryComponents,
    std::string_view childName, std::uint64_t maximumBytes) noexcept;

/// Creates one direct regular-file child with exclusive/no-follow semantics,
/// writes all bytes, and syncs the file. It never replaces an existing entry.
/// parent has the same host-trusted authority requirements as directory
/// creation. The caller must sync the parent (or its containing tree) when the
/// directory entry itself must be crash durable.
VIDEOCUT_BASE_API std::optional<DurableFilesystemError>
WriteNewRegularFileDurably(const std::filesystem::path &parent,
                           std::string_view childName,
                           std::string_view bytes) noexcept;

/// Publication evidence for an atomic metadata replacement. A failed attempt
/// after rename cannot truthfully claim that the old entry is still visible,
/// so callers receive the exact point reached and can retry idempotently.
enum class DurableMetadataPublicationState : std::uint8_t {
  NotPublished = 0,
  VisibleNotSynced = 1,
  DirectorySyncedUnverified = 2,
  DurableVerified = 3,
};

struct DurableMetadataReplaceResult final {
  DurableMetadataPublicationState publicationState{
      DurableMetadataPublicationState::NotPublished};
  std::optional<DurableFilesystemError> error;
  std::uint64_t byteLength{0U};

  [[nodiscard]] bool succeeded() const noexcept {
    return publicationState ==
               DurableMetadataPublicationState::DurableVerified &&
           !error.has_value();
  }
};

/// Replaces one direct regular-file child through one pinned/no-follow parent
/// authority. Success means an exclusive same-directory temporary file was
/// written and synced, atomically renamed over the target, the parent
/// directory was synced, and the published bytes were read back exactly.
VIDEOCUT_BASE_API DurableMetadataReplaceResult ReplaceMetadataFileDurably(
    const std::filesystem::path &parent, std::string_view childName,
    std::string_view bytes) noexcept;

/// Opens a regular file without following the leaf and syncs the bound file
/// object. Identity/size/timestamps must remain stable through the operation.
VIDEOCUT_BASE_API std::optional<DurableFilesystemError>
SyncRegularFileNoFollow(const std::filesystem::path &path) noexcept;

/// Opens a real directory without following the leaf and syncs it.
VIDEOCUT_BASE_API std::optional<DurableFilesystemError>
SyncDirectoryNoFollow(const std::filesystem::path &path) noexcept;

enum class DurableDirectoryEntryType : std::uint8_t {
  RegularFile = 0,
  Directory = 1,
};

struct DurableDirectoryEntry final {
  std::string name;
  DurableDirectoryEntryType type{DurableDirectoryEntryType::RegularFile};
};

using DurableDirectoryListResult =
    std::variant<std::vector<DurableDirectoryEntry>, DurableFilesystemError>;

/// Returns a sorted, bounded snapshot of a real directory's direct children.
/// Every returned name is one component. Symlinks and special entries reject
/// the whole snapshot; directory mutation during enumeration also fails.
VIDEOCUT_BASE_API DurableDirectoryListResult ListDirectoryChildrenNoFollow(
    const std::filesystem::path &path, std::uint64_t maximumChildren) noexcept;

/// Syncs all regular files and directories below root in post-order. Symlinks
/// and non-regular entries are rejected and traversal is strictly bounded.
/// Platforms without descriptor-relative traversal return
/// OperationNotSupported before touching the tree.
VIDEOCUT_BASE_API std::optional<DurableFilesystemError>
SyncTreeNoFollow(const std::filesystem::path &root,
                 const DurableTreeLimits &limits) noexcept;

enum class DurableRenameDisposition : std::uint8_t {
  Renamed = 0,
  DestinationExists = 1,
};

using DurableRenameResult =
    std::variant<DurableRenameDisposition, DurableFilesystemError>;

/// Atomically renames one direct regular-file or directory child without
/// replacing an existing destination. Both names must be single host-generated
/// components and both parents must be host-trusted absolute normalized paths.
/// No check-then-rename fallback is used on unsupported kernels.
VIDEOCUT_BASE_API DurableRenameResult AtomicRenameChildNoReplace(
    const std::filesystem::path &sourceParent, std::string_view sourceName,
    const std::filesystem::path &destinationParent,
    std::string_view destinationName) noexcept;

/// Local-file variant of AtomicRenameChildNoReplace. Names may contain spaces
/// and bounded valid UTF-8, but never separators, control characters, "." or
/// "..". Exact filesystem spelling is retained without Unicode normalization.
/// This does not broaden the portable artifact-name contract above. The same
/// descriptor-bound identity checks and native atomic no-replace operation are
/// required; unsupported platforms fail closed.
VIDEOCUT_BASE_API DurableRenameResult AtomicRenameLocalChildNoReplace(
    const std::filesystem::path &sourceParent, std::string_view sourceName,
    const std::filesystem::path &destinationParent,
    std::string_view destinationName) noexcept;

enum class DurableRemoveDisposition : std::uint8_t {
  Removed = 0,
  NotFound = 1,
};

using DurableRemoveResult =
    std::variant<DurableRemoveDisposition, DurableFilesystemError>;

/// Removes exactly one direct child tree without following symlinks. Traversal
/// is descriptor-relative and bounded. On failure, a bounded prefix may have
/// been removed; this operation never broadens beyond childName. parent is a
/// host-trusted absolute normalized non-root authority. Platforms without
/// descriptor-relative deletion return OperationNotSupported before removal.
VIDEOCUT_BASE_API DurableRemoveResult RemoveChildTreeBounded(
    const std::filesystem::path &parent, std::string_view childName,
    const DurableTreeLimits &limits) noexcept;

/// Moves exactly one direct child into a caller-selected quarantine directory
/// using atomic no-replace rename, then syncs both parents. It never invents a
/// destination name and never accepts a multi-component relative path.
VIDEOCUT_BASE_API DurableRenameResult QuarantineChildNoReplace(
    const std::filesystem::path &sourceParent, std::string_view sourceName,
    const std::filesystem::path &quarantineParent,
    std::string_view quarantineName) noexcept;

enum class DurableAgeCheckedRenameDisposition : std::uint8_t {
  Renamed = 0,
  DestinationExists = 1,
  NotOldEnough = 2,
};

using DurableAgeCheckedRenameResult =
    std::variant<DurableAgeCheckedRenameDisposition, DurableFilesystemError>;

/// Atomically moves one direct regular-file child to quarantine only when the
/// exact descriptor-bound source is at least minimumAge old. The age check and
/// no-replace rename use the same pinned parent authority, closing the race
/// between eligibility and deletion. A negative age is invalid. Unsupported
/// platforms fail closed before moving the child.
VIDEOCUT_BASE_API DurableAgeCheckedRenameResult
QuarantineChildIfOlderThanNoReplace(
    const std::filesystem::path &sourceParent, std::string_view sourceName,
    const std::filesystem::path &quarantineParent,
    std::string_view quarantineName, std::chrono::seconds minimumAge) noexcept;

} // namespace videocut::base
