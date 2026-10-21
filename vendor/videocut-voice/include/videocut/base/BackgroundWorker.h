#pragma once

#include "videocut/base/BaseApi.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace videocut::base {

/// Owns one long-lived worker loop and provides interruptible idle waits.
/// A task may request its own stop. If it destroys the worker or its owner on
/// that thread, destruction must be the task's final owner access.
class VIDEOCUT_BASE_API BackgroundWorker final {
public:
    using Task = std::function<void()>;

    BackgroundWorker();
    ~BackgroundWorker() noexcept;

    BackgroundWorker(const BackgroundWorker&) = delete;
    BackgroundWorker& operator=(const BackgroundWorker&) = delete;

    void Start(Task task, std::string name = {});
    void Stop() noexcept;

    bool StopRequested() const noexcept;
    void Notify() noexcept;
    // Best-effort wake for a realtime consumer. A false result requires the
    // receiving loop to have its own bounded idle wait; no wake is promised.
    bool TryNotify() noexcept;
    bool WaitFor(std::uint32_t timeoutMilliseconds) noexcept;

private:
    struct State;
    static void RequestStop(const std::shared_ptr<State>& state) noexcept;
    static void WaitUntilFinished(
        const std::shared_ptr<State>& state) noexcept;
    static void RetireFinishedThread(std::thread& worker) noexcept;
    static void Execute(const std::shared_ptr<State>& state) noexcept;

    // State has one stable allocation for this object's entire lifetime.
    // Worker hot paths therefore read the stop token without taking the
    // lifecycle mutex; only Start/Stop serialize the thread handle and task.
    const std::shared_ptr<State> m_state;
    mutable std::mutex m_lifecycleMutex;
    std::condition_variable m_lifecycleChanged;
    bool m_stopInProgress{false};
    std::thread m_thread;
};

} // namespace videocut::base
