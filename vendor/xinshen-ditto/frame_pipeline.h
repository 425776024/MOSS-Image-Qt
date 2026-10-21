#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

// One owner per decoder, inference session and encoder. FIFO queues limit
// memory to two waiting frames per boundary, independently of video length.
template <class Prepared, class Output, class Prepare, class Infer, class Write, class Check>
void runFramePipeline(size_t count, Prepare prepare, Infer infer, Write write, Check check) {
  std::mutex mutex;
  std::condition_variable changed;
  std::exception_ptr failure;
  std::deque<Prepared> prepared;
  std::deque<Output> finished;
  auto fail = [&] {
    std::lock_guard<std::mutex> lock(mutex);
    if (!failure) failure = std::current_exception();
    changed.notify_all();
  };
  auto wait = [&](auto& lock, auto ready) {
    while (!failure && !ready()) {
      changed.wait_for(lock, std::chrono::milliseconds(50));
      check();
    }
    if (failure) std::rethrow_exception(failure);
    check();
  };
  auto push = [&](auto& queue, auto value) {
    std::unique_lock<std::mutex> lock(mutex);
    wait(lock, [&] { return queue.size() < 2; });
    queue.push_back(std::move(value));
    changed.notify_all();
  };
  auto pop = [&](auto& queue) {
    std::unique_lock<std::mutex> lock(mutex);
    wait(lock, [&] { return !queue.empty(); });
    auto value = std::move(queue.front());
    queue.pop_front();
    changed.notify_all();
    return value;
  };
  std::vector<std::thread> workers;
  workers.reserve(2);
  try {
    workers.emplace_back([&] {
      try {
        for (size_t i = 0; i < count; ++i) { check(); push(prepared, prepare(i)); }
      } catch (...) { fail(); }
    });
    workers.emplace_back([&] {
      try {
        for (size_t i = 0; i < count; ++i) write(i, pop(finished));
      } catch (...) { fail(); }
    });
    for (size_t i = 0; i < count; ++i) push(finished, infer(pop(prepared)));
  } catch (...) { fail(); }
  for (auto& worker : workers) worker.join();
  if (failure) std::rethrow_exception(failure);
}
