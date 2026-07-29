// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <crayon/thread_pool.hpp>

namespace {

using namespace std::chrono_literals;

int add(int lhs, int rhs) { return lhs + rhs; }

struct Calculator {
  [[nodiscard]]
  int multiply(int lhs, int rhs) const {
    return lhs * rhs;
  }
};

struct Multiply {
  [[nodiscard]]
  int operator()(int lhs, int rhs) const {
    return lhs * rhs;
  }
};

struct MoveOnlyCallable {
  explicit MoveOnlyCallable(int value) : value_{std::make_unique<int>(value)} {}

  MoveOnlyCallable(const MoveOnlyCallable &) = delete;
  MoveOnlyCallable &operator=(const MoveOnlyCallable &) = delete;

  MoveOnlyCallable(MoveOnlyCallable &&) noexcept = default;
  MoveOnlyCallable &operator=(MoveOnlyCallable &&) noexcept = default;

  [[nodiscard]]
  int operator()() && {
    return *value_;
  }

private:
  std::unique_ptr<int> value_;
};

class ThreadPoolConstructionTest : public testing::Test {};

TEST_F(ThreadPoolConstructionTest, AvailableParallelismIsNeverZero) {
  EXPECT_GT(crayon::ThreadPool::available_parallelism(), 0U);
}

TEST_F(ThreadPoolConstructionTest, CreatesRequestedNumberOfThreads) {
  crayon::ThreadPool pool{3};

  EXPECT_EQ(pool.current_num_threads(), 3U);
}

TEST_F(ThreadPoolConstructionTest, ZeroUsesAvailableParallelism) {
  crayon::ThreadPool pool{0};

  EXPECT_EQ(pool.current_num_threads(),
            crayon::ThreadPool::available_parallelism());
}

TEST_F(ThreadPoolConstructionTest, DefaultUsesAvailableParallelism) {
  crayon::ThreadPool pool;

  EXPECT_EQ(pool.current_num_threads(),
            crayon::ThreadPool::available_parallelism());
}

class ThreadPoolSubmissionTest : public testing::Test {};

TEST_F(ThreadPoolSubmissionTest, ExecutesNullaryLambda) {
  crayon::ThreadPool pool{2};

  auto future{pool.submit([]() -> int { return 42; })};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, ExecutesVoidTask) {
  crayon::ThreadPool pool{2};
  std::atomic<bool> executed{false};

  auto future{pool.submit([&executed]() -> void {
    executed.store(true, std::memory_order_release);
  })};

  future.get();

  EXPECT_TRUE(executed.load(std::memory_order_acquire));
}

TEST_F(ThreadPoolSubmissionTest, PassesArgumentsToCallable) {
  crayon::ThreadPool pool{2};

  auto future{pool.submit(
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [](std::string value, std::size_t count) -> std::string {
        std::string result;
        for (std::size_t i{0}; i < count; ++i) {
          result += value;
        }

        return result;
      },
      std::string{"ab"}, 3U)};

  EXPECT_EQ(future.get(), "ababab");
}

TEST_F(ThreadPoolSubmissionTest, ExecutesFreeFunction) {
  crayon::ThreadPool pool{2};

  auto future{pool.submit(add, 20, 22)};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, ExecutesFunctionPointer) {
  crayon::ThreadPool pool{2};
  int (*function)(int, int){add};

  auto future{pool.submit(function, 20, 22)};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, ExecutesMemberFunctionWithObjectPointer) {
  crayon::ThreadPool pool{2};
  Calculator calculator;

  auto future{pool.submit(&Calculator::multiply, &calculator, 6, 7)};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, ExecutesMemberFunctionWithObjectCopy) {
  crayon::ThreadPool pool{2};
  Calculator calculator;

  auto future{pool.submit(&Calculator::multiply, calculator, 6, 7)};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, ExecutesFunctionObject) {
  crayon::ThreadPool pool{2};

  auto future{pool.submit(Multiply{}, 6, 7)};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, SupportsMoveOnlyCallable) {
  crayon::ThreadPool pool{2};

  auto future{pool.submit(MoveOnlyCallable{42})};

  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, SupportsMoveOnlyArgument) {
  crayon::ThreadPool pool{2};

  auto value{std::make_unique<int>(42)};

  auto future{pool.submit(
      [](std::unique_ptr<int> argument) -> int { return *argument; },
      std::move(value))};

  EXPECT_EQ(value, nullptr);
  EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, DecayCopiesArgumentsByDefault) {
  crayon::ThreadPool pool{1};

  std::promise<void> release_task;
  auto release_future{release_task.get_future().share()};

  int value{21};

  auto future{pool.submit(
      [release_future](int argument) -> int {
        release_future.wait();
        return argument * 2;
      },
      value)};

  value = 100;
  release_task.set_value();

  EXPECT_EQ(future.get(), 42);
  EXPECT_EQ(value, 100);
}

TEST_F(ThreadPoolSubmissionTest, SupportsExplicitReferenceArguments) {
  crayon::ThreadPool pool{1};

  int value{21};

  auto future{pool.submit([](int &argument) -> void { argument *= 2; },
                          std::ref(value))};

  future.get();

  EXPECT_EQ(value, 42);
}

TEST_F(ThreadPoolSubmissionTest, PropagatesTaskExceptionsThroughFuture) {
  crayon::ThreadPool pool{2};

  auto future{
      pool.submit([]() -> int { throw std::runtime_error{"task failure"}; })};

  EXPECT_THROW(
      {
        try {
          static_cast<void>(future.get());
        } catch (const std::runtime_error &error) {
          EXPECT_STREQ(error.what(), "task failure");
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(ThreadPoolSubmissionTest, TrySubmitReturnsFutureOnSuccess) {
  crayon::ThreadPool pool{2};

  auto maybe_future{pool.try_submit([]() -> int { return 42; })};

  ASSERT_TRUE(maybe_future.has_value());

  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(maybe_future->get(), 42);
}

TEST_F(ThreadPoolSubmissionTest, ExecutesManyTasks) {
  constexpr std::size_t task_count{1'000};

  crayon::ThreadPool pool{4};
  std::vector<std::future<std::size_t>> futures;
  futures.reserve(task_count);

  for (std::size_t i{0}; i < task_count; ++i) {
    futures.emplace_back(pool.submit([i]() -> std::size_t { return i * i; }));
  }

  for (std::size_t i{0}; i < task_count; ++i) {
    EXPECT_EQ(futures[i].get(), i * i);
  }
}

class ThreadPoolConcurrencyTest : public testing::Test {};

TEST_F(ThreadPoolConcurrencyTest, SupportsConcurrentProducers) {
  constexpr std::size_t producer_count{8};
  constexpr std::size_t tasks_per_producer{250};
  constexpr std::size_t expected_task_count{producer_count *
                                            tasks_per_producer};

  crayon::ThreadPool pool{4};

  std::atomic<std::size_t> executed_tasks{0};
  std::mutex futures_mutex;
  std::vector<std::future<void>> futures;
  futures.reserve(expected_task_count);

  std::vector<std::thread> producers;
  producers.reserve(producer_count);

  for (std::size_t producer{0}; producer < producer_count; ++producer) {
    producers.emplace_back(
        [&pool, &executed_tasks, &futures, &futures_mutex]() -> void {
          for (std::size_t i{0}; i < tasks_per_producer; ++i) {
            auto future{pool.submit([&executed_tasks]() -> void {
              executed_tasks.fetch_add(1, std::memory_order_relaxed);
            })};

            std::lock_guard lock{futures_mutex};
            futures.emplace_back(std::move(future));
          }
        });
  }

  for (auto &producer : producers) {
    producer.join();
  }

  for (auto &future : futures) {
    future.get();
  }

  EXPECT_EQ(executed_tasks.load(std::memory_order_relaxed),
            expected_task_count);
}

TEST_F(ThreadPoolConcurrencyTest, SubmitRacingWithNonBlockingShutdownIsSafe) {
  constexpr std::size_t producer_count{8};
  constexpr std::size_t attempts_per_producer{250};

  crayon::ThreadPool pool{4};

  std::atomic<bool> begin{false};
  std::atomic<std::size_t> executed{0};
  std::atomic<std::size_t> rejected{0};

  std::mutex futures_mutex;
  std::vector<std::future<void>> accepted_futures;

  // Ensure the test always contains at least one accepted task.
  accepted_futures.emplace_back(pool.submit([&executed]() -> void {
    executed.fetch_add(1, std::memory_order_relaxed);
  }));

  std::vector<std::thread> producers;
  producers.reserve(producer_count);

  for (std::size_t producer{0}; producer < producer_count; ++producer) {
    producers.emplace_back([&pool, &begin, &executed, &rejected,
                            &accepted_futures, &futures_mutex]() -> void {
      while (!begin.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      for (std::size_t i{0}; i < attempts_per_producer; ++i) {
        auto maybe_future{pool.try_submit([&executed]() -> void {
          executed.fetch_add(1, std::memory_order_relaxed);
        })};

        if (!maybe_future) {
          rejected.fetch_add(1, std::memory_order_relaxed);
          continue;
        }

        std::lock_guard lock{futures_mutex};
        accepted_futures.emplace_back(std::move(*maybe_future));
      }
    });
  }

  std::thread shutdown_thread{[&pool, &begin]() -> void {
    while (!begin.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    pool.shutdown();
  }};

  begin.store(true, std::memory_order_release);

  for (auto &producer : producers) {
    producer.join();
  }

  shutdown_thread.join();

  // A later blocking shutdown is allowed after the non-blocking request.
  pool.shutdown(true);

  for (auto &future : accepted_futures) {
    future.get();
  }

  EXPECT_EQ(executed.load(std::memory_order_relaxed), accepted_futures.size());

  // Rejection is deterministic after shutdown has completed.
  EXPECT_FALSE(pool.try_submit([]() -> void {}).has_value());
  EXPECT_GT(rejected.load(std::memory_order_relaxed) + accepted_futures.size(),
            0U);
}

TEST_F(ThreadPoolConcurrencyTest,
       MultipleConcurrentNonBlockingShutdownRequestsAreSafe) {
  constexpr std::size_t caller_count{16};

  crayon::ThreadPool pool{4};
  std::vector<std::thread> callers;
  callers.reserve(caller_count);

  for (std::size_t i{0}; i < caller_count; ++i) {
    callers.emplace_back([&pool]() -> void { pool.shutdown(); });
  }

  for (auto &caller : callers) {
    caller.join();
  }

  EXPECT_TRUE(pool.is_shutting_down());

  // Perform the single externally serialized blocking wait.
  pool.shutdown(true);
}

class ThreadPoolShutdownTest : public testing::Test {};

TEST_F(ThreadPoolShutdownTest, StartsInRunningState) {
  crayon::ThreadPool pool{2};

  EXPECT_FALSE(pool.is_shutting_down());
}

TEST_F(ThreadPoolShutdownTest, NonBlockingShutdownMarksPoolAsShuttingDown) {
  crayon::ThreadPool pool{2};

  pool.shutdown();

  EXPECT_TRUE(pool.is_shutting_down());
}

TEST_F(ThreadPoolShutdownTest, NonBlockingShutdownReturnsBeforeTaskFinishes) {
  crayon::ThreadPool pool{1};

  std::promise<void> task_started;
  auto task_started_future{task_started.get_future()};

  std::promise<void> release_task;
  auto release_future{release_task.get_future().share()};

  auto task_future{pool.submit([&task_started, release_future]() -> void {
    task_started.set_value();
    release_future.wait();
  })};

  task_started_future.get();

  pool.shutdown();

  EXPECT_TRUE(pool.is_shutting_down());
  EXPECT_EQ(task_future.wait_for(0ms), std::future_status::timeout);

  release_task.set_value();
  task_future.get();

  pool.shutdown(true);
}

TEST_F(ThreadPoolShutdownTest, BlockingShutdownDrainsQueuedTasks) {
  constexpr std::size_t task_count{500};

  crayon::ThreadPool pool{4};
  std::atomic<std::size_t> completed{0};

  for (std::size_t i{0}; i < task_count; ++i) {
    static_cast<void>(pool.submit([&completed]() -> void {
      completed.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  pool.shutdown(true);

  EXPECT_EQ(completed.load(std::memory_order_relaxed), task_count);
  EXPECT_TRUE(pool.is_shutting_down());
}

TEST_F(ThreadPoolShutdownTest, DestructorDrainsQueuedTasks) {
  constexpr std::size_t task_count{500};

  std::atomic<std::size_t> completed{0};
  std::vector<std::future<void>> futures;
  futures.reserve(task_count);

  {
    crayon::ThreadPool pool{4};

    for (std::size_t i{0}; i < task_count; ++i) {
      futures.emplace_back(pool.submit([&completed]() -> void {
        completed.fetch_add(1, std::memory_order_relaxed);
      }));
    }
  }

  EXPECT_EQ(completed.load(std::memory_order_relaxed), task_count);

  for (auto &future : futures) {
    EXPECT_NO_THROW(future.get());
  }
}

TEST_F(ThreadPoolShutdownTest, ShutdownIsIdempotent) {
  crayon::ThreadPool pool{2};

  pool.shutdown();
  pool.shutdown();
  pool.shutdown(false);
  pool.shutdown(true);
  pool.shutdown(true);

  EXPECT_TRUE(pool.is_shutting_down());
}

TEST_F(ThreadPoolShutdownTest, TrySubmitReturnsNulloptAfterShutdown) {
  crayon::ThreadPool pool{2};

  pool.shutdown();

  auto maybe_future{pool.try_submit([]() -> int { return 42; })};

  EXPECT_FALSE(maybe_future.has_value());
}

TEST_F(ThreadPoolShutdownTest, SubmitThrowsAfterShutdown) {
  crayon::ThreadPool pool{2};

  pool.shutdown();

  EXPECT_THROW(static_cast<void>(pool.submit([]() -> int { return 42; })),
               std::runtime_error);
}

TEST_F(ThreadPoolShutdownTest, RejectedTaskIsNeverExecuted) {
  crayon::ThreadPool pool{2};
  std::atomic<bool> executed{false};

  pool.shutdown();

  auto maybe_future{pool.try_submit([&executed]() -> void {
    executed.store(true, std::memory_order_release);
  })};

  EXPECT_FALSE(maybe_future.has_value());

  pool.shutdown(true);

  EXPECT_FALSE(executed.load(std::memory_order_acquire));
}

TEST_F(ThreadPoolShutdownTest,
       ThreadCountRemainsNumberOfOwnedWorkersAfterJoin) {
  crayon::ThreadPool pool{3};

  pool.shutdown(true);

  EXPECT_EQ(pool.current_num_threads(), 3U);
}

class ThreadPoolRecursiveSubmissionTest : public testing::Test {};

TEST_F(ThreadPoolRecursiveSubmissionTest,
       WorkerCanSubmitAdditionalTasksWithoutWaitingForThem) {
  constexpr std::size_t child_count{32};

  crayon::ThreadPool pool{4};

  std::promise<std::vector<std::future<std::size_t>>> child_futures_promise;
  auto child_futures_future{child_futures_promise.get_future()};

  auto parent_future{pool.submit([&pool, &child_futures_promise]() -> void {
    std::vector<std::future<std::size_t>> child_futures;
    child_futures.reserve(child_count);

    for (std::size_t i{0}; i < child_count; ++i) {
      child_futures.emplace_back(
          pool.submit([i]() -> std::size_t { return i; }));
    }

    child_futures_promise.set_value(std::move(child_futures));
  })};

  parent_future.get();

  auto child_futures{child_futures_future.get()};

  std::size_t sum{0};
  for (auto &future : child_futures) {
    sum += future.get();
  }

  constexpr std::size_t expected_sum{(child_count - 1) * child_count / 2};

  EXPECT_EQ(sum, expected_sum);
}

} // namespace
