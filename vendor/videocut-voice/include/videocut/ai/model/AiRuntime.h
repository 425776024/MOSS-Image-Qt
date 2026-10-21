#pragma once

#include "videocut/ai/Inference.h"
#include "videocut/ai/model/ModelSource.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace videocut::ai::model {

namespace detail { class InferenceExecutionQueue; struct AiMemoryReservation; }

enum class InferenceIntent : std::uint8_t {
  Export = 0,
  Interactive = 1,
  Background = 2,
};

enum class AiMemoryPressure : std::uint8_t { Normal, Warning, Critical };

struct InferenceExecutionOptions final {
  InferenceIntent intent{InferenceIntent::Interactive};
  /// Absolute steady-clock nanoseconds; zero means no wall-clock deadline.
  std::int64_t deadlineSteadyClockNs{0};
};

using InferenceBatchResult = Result<std::vector<InferenceResponse>>;
using InferenceBatchCompletion = std::function<void(InferenceBatchResult)>;

/// A nonblocking cancellation owner. Releasing it cancels queued work and
/// requests cancellation of running work at the backend's safe boundaries.
/// Completion still runs exactly once for every admitted request.
class InferenceTicket final {
public:
  InferenceTicket() noexcept = default;
  ~InferenceTicket();
  InferenceTicket(InferenceTicket &&) noexcept;
  InferenceTicket &operator=(InferenceTicket &&) noexcept;
  InferenceTicket(const InferenceTicket &) = delete;
  InferenceTicket &operator=(const InferenceTicket &) = delete;
  void Cancel() noexcept;
  [[nodiscard]] bool Finished() const noexcept;

private:
  struct State;
  explicit InferenceTicket(std::shared_ptr<State> state) noexcept;
  std::shared_ptr<State> state_;
  friend class detail::InferenceExecutionQueue;
};

class IInferenceBackendFactory {
public:
  virtual ~IInferenceBackendFactory() = default;

  [[nodiscard]] virtual std::string RuntimeId() const = 0;
  [[nodiscard]] virtual std::string RuntimeVersion() const = 0;
  [[nodiscard]] virtual ComputeBackend Backend() const noexcept = 0;
  [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;

  virtual Result<std::shared_ptr<IInferenceSession>>
  CreateSession(const ResolvedModel &model) = 0;
};

struct SessionRequest final {
  ModelRequest model;
  /// Omit to use AiRuntimeOptions::defaultComputePreference.
  std::optional<ComputePreference> computePreference;
  std::optional<SessionResidency> residencyOverride;
  /// Also applies while creating a cold backend session so its worker pools
  /// inherit the intended priority. Cached sessions remain single-flight.
  InferenceExecutionOptions executionOptions;
};

struct AiRuntimeOptions final {
  ComputePreference defaultComputePreference{ComputePreference::ModelPreferred};
  std::size_t executionWorkers{2U};
  /// The process queue reserves one Host admission plus one model-child slot
  /// per Host worker for Export. Standalone model-only queues use the full cap.
  std::size_t maximumPendingBatches{64U};
  std::uint64_t maximumInferenceInputBytes{512ULL * 1024ULL * 1024ULL};
  /// Zero chooses one third of physical RAM, capped at 8 GiB. This is an
  /// admission bound for reported model residency and declared working sets,
  /// not a measurement or hard limit of process RSS.
  std::uint64_t maximumResidentBytes{0U};
  /// Zero disables time-based eviction; pressure eviction remains enabled.
  std::uint64_t keepWarmIdleTimeoutMillis{300'000U};
};

struct AiRuntimeStats final {
  std::size_t cacheEntries{0};
  std::size_t liveSessions{0};
  std::size_t residentSessions{0};
  std::uint64_t liveResidentBytes{0};
  std::uint64_t retainedResidentBytes{0};
  std::uint64_t sessionCreations{0};
  std::uint64_t sessionCacheHits{0};
  std::uint64_t lruEvictions{0};
  bool shuttingDown{false};
  std::size_t queuedBatches{0U};
  std::size_t runningBatches{0U};
  std::size_t runningBackgroundBatches{0U};
  std::size_t maximumBackgroundBatches{0U};
  std::size_t queuedDependencies{0U};
  std::size_t runningDependencies{0U};
  std::uint64_t inferenceInputBytes{0U};
  std::uint64_t physicalMemoryBytes{0U};
  std::uint64_t residentBudgetBytes{0U};
  std::uint64_t reservedResidentBytes{0U};
  std::uint64_t creatingResidentBytes{0U};
  std::uint64_t workingSetReservedBytes{0U};
  std::uint64_t residentAdmissionRejections{0U};
  std::uint64_t backgroundPressureRejections{0U};
  std::uint64_t idleEvictions{0U};
  std::uint64_t pressureEvictions{0U};
  std::uint64_t pressureEvents{0U};
  std::uint64_t keepWarmIdleTimeoutMillis{0U};
  AiMemoryPressure memoryPressure{AiMemoryPressure::Normal};
  bool systemMemoryPressureMonitoring{false};
};

/// Shares the same atomic admission budget as model creation/residency.
/// Releasing a task (including cancellation or rejection) returns its charge.
class AiWorkingSetLease final {
public:
  AiWorkingSetLease() = default;
  AiWorkingSetLease(AiWorkingSetLease &&) noexcept = default;
  AiWorkingSetLease &operator=(AiWorkingSetLease &&) noexcept = default;
  AiWorkingSetLease(const AiWorkingSetLease &) = delete;
  AiWorkingSetLease &operator=(const AiWorkingSetLease &) = delete;
private:
  explicit AiWorkingSetLease(std::shared_ptr<detail::AiMemoryReservation> value)
      : reservation_(std::move(value)) {}
  std::shared_ptr<detail::AiMemoryReservation> reservation_;
  friend class AiRuntime;
  friend class ProcessAiRuntime;
};

struct AiRuntimeTrimResult final {
  std::size_t evictedSessions{0};
  std::uint64_t retainedBytesBefore{0};
  std::uint64_t retainedBytesAfter{0};
  std::uint64_t requestedBudgetBytes{0};
};

/// Exact lease for a non-weight support file owned by the same immutable
/// ModelPack source as runtime sessions. Protected Bundle sources retain their
/// transient materialization through [packLease]; callers never reopen or
/// independently resolve the pack.
struct ResolvedModelSupportFileLease final {
  ModelSpec model;
  std::string role;
  std::filesystem::path path;
  std::uint64_t bytes{0U};
  std::string sha256;
  std::shared_ptr<const ResolvedModelPackLease> packLease;
};

class SessionLease final {
public:
  SessionLease() = default;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] const ModelSpec &Model() const;
  [[nodiscard]] ActualComputePath ComputePath() const;
  [[nodiscard]] std::uint64_t ResidentBytes() const noexcept;
  [[nodiscard]] std::size_t MaximumBatchSize() const noexcept;
  [[nodiscard]] SessionLease WithExecutionOptions(
      InferenceExecutionOptions options) const;

  /// Prepares metadata only on this session's existing bounded model queue.
  /// Serialized with inference, with identical priority/export/cancellation
  /// admission. The calling preparation worker waits for real backend retirement
  /// even if an uninterruptible constructor observes cancellation only on return.
  /// Do not invoke synchronously from a model execution/completion callback.
  [[nodiscard]] AiError PrepareProfile(
      InferenceProfile profile, std::function<bool()> cancellation = {},
      std::optional<InferenceExecutionOptions> options = std::nullopt) const;

  [[nodiscard]] Result<std::vector<InferenceResponse>>
  RunBatch(const std::vector<InferenceRequest> &requests,
           std::optional<InferenceExecutionOptions> options = std::nullopt) const;

  /// Takes immutable tensor ownership and never waits for another inference.
  /// Rejected submissions do not invoke completion. Completion must not throw
  /// or synchronously wait for another request on this runtime.
  [[nodiscard]] Result<InferenceTicket>
  SubmitBatch(std::vector<InferenceRequest> requests,
              InferenceExecutionOptions options,
              InferenceBatchCompletion completion) const;

private:
  struct Impl;
  explicit SessionLease(std::shared_ptr<Impl> implementation);

  std::shared_ptr<Impl> m_implementation;
  InferenceExecutionOptions executionOptions_;

  friend class AiRuntime;
};

/// Process-capable session owner. A host may retain one AiRuntime for the
/// application lifetime, while standalone tests can construct isolated
/// instances. Backend sessions never leak into Flutter or editor contracts.
class AiRuntime final {
public:
  explicit AiRuntime(std::shared_ptr<IModelSource> modelSource,
                     AiRuntimeOptions options = {});
  ~AiRuntime();

  AiRuntime(const AiRuntime &) = delete;
  AiRuntime &operator=(const AiRuntime &) = delete;

  [[nodiscard]] AiError
  RegisterBackendFactory(std::shared_ptr<IInferenceBackendFactory> factory);
  [[nodiscard]] Result<SessionLease> AcquireSession(SessionRequest request);
  [[nodiscard]] Result<std::vector<InferenceResponse>>
  RunBatch(SessionRequest request, const std::vector<InferenceRequest> &batch);

  /// Returns the immutable model-source catalog and its current local
  /// availability. The runtime remains the sole source owner; callers receive
  /// a value snapshot and cannot resolve files or mutate catalog state.
  [[nodiscard]] std::vector<ModelAvailability> InspectModels() const;
  // Declared identities only; no payload verification or materialization.
  [[nodiscard]] std::vector<ModelSpec> ModelDeclarations() const;
  [[nodiscard]] Result<std::vector<ModelLicenseDocument>>
  ReadModelLicenses(const ModelRequest &exact) const;

  [[nodiscard]] Result<ResolvedModelSupportFileLease>
  AcquireSupportFile(ModelRequest exact, std::string role) const;

  /// Drops cache ownership of least-recently-used non-process-resident
  /// sessions until retained model memory fits [maximumRetainedBytes].
  /// Outstanding inference leases remain valid and are never interrupted.
  [[nodiscard]] AiRuntimeTrimResult
  TrimRetainedSessionsToBudget(std::uint64_t maximumRetainedBytes);

  /// As above, but does not evict retained sessions for [preservedModelId].
  /// Hosts use this during synchronous memory admission so repeated work can
  /// reuse the requested model while older, unrelated KeepWarm sessions are
  /// reclaimed first.
  [[nodiscard]] AiRuntimeTrimResult
  TrimRetainedSessionsToBudget(std::uint64_t maximumRetainedBytes,
                               std::string_view preservedModelId);

  /// Releases OnDemand/KeepWarm cache ownership. ProcessResident sessions are
  /// retained until Shutdown, while outstanding leases remain valid.
  void EvictIdleSessions();
  /// Queued background/preview inference yields at the next safe batch
  /// boundary during the application's existing exclusive export interval.
  void SetExportExclusive(bool exclusive);
  /// Local, scoped pressure input for hosts/diagnostics. Apple system pressure
  /// uses this same control state; Normal resumes new Background admission.
  void SetMemoryPressure(AiMemoryPressure pressure);
  [[nodiscard]] AiError SetKeepWarmIdleTimeout(std::uint64_t milliseconds);
  [[nodiscard]] Result<AiWorkingSetLease>
  AcquireWorkingSet(std::uint64_t bytes, InferenceIntent intent = InferenceIntent::Background);
  void Shutdown();
  [[nodiscard]] AiRuntimeStats Stats() const;

private:
  AiRuntime(std::shared_ptr<IModelSource>, AiRuntimeOptions,
            std::shared_ptr<detail::InferenceExecutionQueue>, bool ownsExecution);
  friend class ProcessAiRuntime;
  struct Impl;
  std::unique_ptr<Impl> m_implementation;
};

/// Optional application-wide holder. Installation is explicit so unit tests
/// and tools do not accidentally share process-global state.
class InferenceExportPriorityLease final {
public:
  ~InferenceExportPriorityLease();
  InferenceExportPriorityLease(const InferenceExportPriorityLease &) = delete;
  InferenceExportPriorityLease &operator=(const InferenceExportPriorityLease &) = delete;
private:
  InferenceExportPriorityLease() = default;
  bool active_{false};
  friend class ProcessAiRuntime;
};

class ProcessAiRuntime final {
public:
  /// Product runtimes and frame dependencies share the same execution owner.
  /// Creating that owner does not load models or require a model catalog.
  [[nodiscard]] static std::shared_ptr<AiRuntime>
  Create(std::shared_ptr<IModelSource> source, AiRuntimeOptions options = {});
  /// Executes a bounded Host dependency under the existing inference admission,
  /// deadline, cancellation and export rules. Frame leases remain with work;
  /// tensor allocations are charged when each model batch is admitted.
  /// Cancellation and completion callbacks must not throw. Cancellation is a
  /// nonblocking observation under the admission lock: it must not reenter
  /// AiRuntime/ProcessAiRuntime or wait for work on this queue.
  [[nodiscard]] static AiError DispatchDependency(
      InferenceExecutionOptions options, std::function<bool()> cancellation,
      std::function<void()> work, std::function<void(std::string)> completion);
  [[nodiscard]] static AiError Install(std::shared_ptr<AiRuntime> runtime);
  [[nodiscard]] static std::shared_ptr<AiRuntime> Get();
  /// Also available before a model catalog is installed, for native jobs.
  [[nodiscard]] static Result<AiWorkingSetLease>
  AcquireWorkingSet(std::uint64_t bytes, InferenceIntent intent = InferenceIntent::Background);
  [[nodiscard]] static std::shared_ptr<InferenceExportPriorityLease>
  AcquireExportPriority();
  /// Closes and drains from an external control thread. Calling from a model,
  /// Host or completion worker throws std::logic_error before waiting; callback
  /// owners must catch that error or schedule closure on their control owner.
  static void Shutdown();
};

} // namespace videocut::ai::model
