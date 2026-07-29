// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

#include <crayon/thread_pool.hpp>

namespace crayon {

std::size_t ThreadPool::available_parallelism() noexcept {
  // `std::jthread::hardware_concurrency()` is only a hint and may return zero.
  // Ensure the pool always has at least one worker.
  auto const hardware_concurrency{std::jthread::hardware_concurrency()};
  return hardware_concurrency == 0 ? 1 : hardware_concurrency;
}

ThreadPool::ThreadPool() : ThreadPool{0} {}
ThreadPool::ThreadPool(std::size_t num_threads) {
  if (num_threads == 0) {
    num_threads = available_parallelism();
  }

  workers_.reserve(num_threads);
  try {
    for (std::size_t i{0}; i < num_threads; ++i) {
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      workers_.emplace_back([this](std::stop_token stop_token) -> void {
        worker_loop(stop_token);
      });
    }
  } catch (...) {
    // Thread creation may fail after some workers have already started. Signal
    // those workers to exit before allowing construction to fail. Join them
    // while this object's synchronization state is still alive.
    shutdown(true);
    throw;
  }
}

bool ThreadPool::try_enqueue_task(ThreadPool::task_t &&task) {
  std::lock_guard lock{mutex_};
  if (is_shutting_down_) {
    // Reject submissions after shutdown begins. Otherwise a caller could
    // receive a future for work that will never execute.
    return false;
  }

  // `std::move_only_function` is used instead of `std::function` because
  // `std::packaged_task` is move-only.
  tasks_.emplace(std::move(task));
  return true;
}

bool ThreadPool::try_enqueue_task_and_notify_one(ThreadPool::task_t &&task) {
  auto const is_enqueue_successful{try_enqueue_task(std::move(task))};
  if (is_enqueue_successful) {
    // One newly queued task requires waking at most one worker.
    condition_.notify_one();
  }

  return is_enqueue_successful;
}

std::optional<ThreadPool::task_t>
// NOLINTNEXTLINE(performance-unnecessary-value-param)
ThreadPool::try_wait_and_pop_task(std::stop_token stop_token) {
  std::unique_lock lock{mutex_};

  // The predicate protects against spurious wakeups. A worker wakes only when
  // work exists or shutdown has started.
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  condition_.wait(lock, stop_token, [this]() -> bool {
    return is_shutting_down_ || !tasks_.empty();
  });
  if (tasks_.empty()) {
    // During graceful shutdown, workers continue processing until the queue is
    // drained empty.
    return std::nullopt;
  }

  auto task{std::move(tasks_.front())};
  tasks_.pop();

  return task;
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void ThreadPool::worker_loop(std::stop_token stop_token) {
  // Process tasks as long as they are or will be available.
  while (auto task{try_wait_and_pop_task(stop_token)}) {
    // Execute outside the mutex so other workers can dequeue tasks and
    // submitters can enqueue new ones concurrently.
    //
    // `std::packaged_task` captures exceptions and makes them observable
    // through the corresponding future.
    (*task)();
  }
}

bool ThreadPool::try_shutdown() noexcept {
  std::lock_guard lock{mutex_};
  if (is_shutting_down_) {
    // `ThreadPool::try_shutdown()` may be called explicitly and then again by
    // the destructor, so repeated calls must be harmless.
    return false;
  }

  is_shutting_down_ = true;
  return true;
}

bool ThreadPool::try_shutdown_and_notify_all() noexcept {
  if (!try_shutdown()) {
    return false;
  }

  // Wake every worker so idle workers can observe the stop condition.
  for (auto &worker : workers_) {
    worker.request_stop();
  }

  condition_.notify_all();
  return true;
}

std::size_t ThreadPool::current_num_threads() const {
  std::lock_guard lock{mutex_};
  return workers_.size();
}

bool ThreadPool::is_shutting_down() const {
  std::lock_guard lock{mutex_};
  return is_shutting_down_;
}

void ThreadPool::shutdown() noexcept { shutdown(false); }
void ThreadPool::shutdown(bool const wait) noexcept {
  (void)try_shutdown_and_notify_all();
  if (!wait) {
    return;
  }

  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

ThreadPool::~ThreadPool() noexcept {
  // Request shutdown and exit immediately to let `std::vector`'s destructor
  // call `std::jthread`'s destructor to join and destroy all the threads.
  shutdown();
}

} // namespace crayon
