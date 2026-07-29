// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <crayon/this_thread.hpp>
#include <crayon/thread_pool.hpp>

namespace {

using namespace std::chrono_literals;

TEST(ThisThreadTest, IsNotPoolThreadOnTestThread) {
  EXPECT_FALSE(crayon::this_thread::is_pool_thread());
}

TEST(ThisThreadTest, PoolIsNullOnTestThread) {
  EXPECT_EQ(crayon::this_thread::get_pool(), nullptr);
}

TEST(ThisThreadTest, PoolIndexIsEmptyOnTestThread) {
  EXPECT_EQ(crayon::this_thread::get_pool_index(), std::nullopt);
}

TEST(ThisThreadTest, StopTokenHasNoStopStateOnTestThread) {
  std::stop_token const token = crayon::this_thread::get_stop_token();

  EXPECT_FALSE(token.stop_possible());
  EXPECT_FALSE(token.stop_requested());
}

TEST(ThisThreadTest, IsNotPoolThreadOnUserCreatedThread) {
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  std::jthread thread{
      [&promise] { promise.set_value(crayon::this_thread::is_pool_thread()); }};

  EXPECT_FALSE(future.get());
}

TEST(ThisThreadTest, ContextIsEmptyOnUserCreatedThread) {
  struct Result {
    bool is_pool_thread;
    crayon::ThreadPool *pool;
    std::optional<std::size_t> pool_index;
    std::stop_token stop_token;
  };

  std::promise<Result> promise;
  std::future<Result> future = promise.get_future();

  std::jthread thread{[&promise] {
    promise.set_value(Result{
        .is_pool_thread = crayon::this_thread::is_pool_thread(),
        .pool = crayon::this_thread::get_pool(),
        .pool_index = crayon::this_thread::get_pool_index(),
        .stop_token = crayon::this_thread::get_stop_token(),
    });
  }};

  Result const result = future.get();

  EXPECT_FALSE(result.is_pool_thread);
  EXPECT_EQ(result.pool, nullptr);
  EXPECT_EQ(result.pool_index, std::nullopt);
  EXPECT_FALSE(result.stop_token.stop_possible());
  EXPECT_FALSE(result.stop_token.stop_requested());
}

TEST(ThisThreadTest, ReportsPoolThreadInsideTask) {
  crayon::ThreadPool pool{1};

  auto future =
      pool.submit([] { return crayon::this_thread::is_pool_thread(); });

  EXPECT_TRUE(future.get());
}

TEST(ThisThreadTest, ReportsAssociatedPoolInsideTask) {
  crayon::ThreadPool pool{1};

  auto future = pool.submit([] { return crayon::this_thread::get_pool(); });

  EXPECT_EQ(future.get(), &pool);
}

TEST(ThisThreadTest, ReportsPoolIndexInsideTask) {
  crayon::ThreadPool pool{1};

  auto future =
      pool.submit([] { return crayon::this_thread::get_pool_index(); });

  std::optional<std::size_t> const index = future.get();

  ASSERT_TRUE(index.has_value());

  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*index, 0U);
}

TEST(ThisThreadTest, PoolIndexIsStableWithinTask) {
  crayon::ThreadPool pool{1};

  auto future = pool.submit([] {
    auto const first = crayon::this_thread::get_pool_index();

    std::this_thread::yield();

    auto const second = crayon::this_thread::get_pool_index();

    return std::pair{first, second};
  });

  auto const [first, second] = future.get();

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first, second);
}

TEST(ThisThreadTest, PoolIndexIsStableAcrossTasksOnSingleWorker) {
  crayon::ThreadPool pool{1};

  auto first_future =
      pool.submit([] { return crayon::this_thread::get_pool_index(); });

  auto second_future =
      pool.submit([] { return crayon::this_thread::get_pool_index(); });

  auto const first = first_future.get();
  auto const second = second_future.get();

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  EXPECT_EQ(first, 0U);
  EXPECT_EQ(second, 0U);
}

TEST(ThisThreadTest, StopTokenHasStopStateInsideTask) {
  crayon::ThreadPool pool{1};

  auto future =
      pool.submit([] { return crayon::this_thread::get_stop_token(); });

  std::stop_token const token = future.get();

  EXPECT_TRUE(token.stop_possible());
  EXPECT_FALSE(token.stop_requested());
}

TEST(ThisThreadTest, StopTokenObservesPoolStopRequest) {
  crayon::ThreadPool pool{1};

  std::promise<std::stop_token> token_promise;
  std::future<std::stop_token> token_future = token_promise.get_future();

  std::promise<void> release_promise;
  std::shared_future<void> release_future =
      release_promise.get_future().share();

  auto task = pool.submit([&token_promise, release_future] {
    token_promise.set_value(crayon::this_thread::get_stop_token());
    release_future.wait();
  });

  std::stop_token const token = token_future.get();

  ASSERT_TRUE(token.stop_possible());
  EXPECT_FALSE(token.stop_requested());

  pool.shutdown();

  EXPECT_TRUE(token.stop_requested());

  release_promise.set_value();
  task.get();
}

TEST(ThisThreadTest, MultipleTasksReportSamePool) {
  constexpr std::size_t worker_count = 4;
  constexpr std::size_t task_count = 32;

  crayon::ThreadPool pool{worker_count};

  std::vector<std::future<crayon::ThreadPool *>> futures;
  futures.reserve(task_count);

  for (std::size_t index = 0; index < task_count; ++index) {
    futures.push_back(
        pool.submit([] { return crayon::this_thread::get_pool(); }));
  }

  for (auto &future : futures) {
    EXPECT_EQ(future.get(), &pool);
  }
}

TEST(ThisThreadTest, DifferentPoolsReportTheirOwnPool) {
  crayon::ThreadPool first_pool{1};
  crayon::ThreadPool second_pool{1};

  auto first_future =
      first_pool.submit([] { return crayon::this_thread::get_pool(); });

  auto second_future =
      second_pool.submit([] { return crayon::this_thread::get_pool(); });

  EXPECT_EQ(first_future.get(), &first_pool);
  EXPECT_EQ(second_future.get(), &second_pool);
}

TEST(ThisThreadTest, DifferentPoolsUseIndependentWorkerIndices) {
  crayon::ThreadPool first_pool{1};
  crayon::ThreadPool second_pool{1};

  auto first_future =
      first_pool.submit([] { return crayon::this_thread::get_pool_index(); });

  auto second_future =
      second_pool.submit([] { return crayon::this_thread::get_pool_index(); });

  auto const first_index = first_future.get();
  auto const second_index = second_future.get();

  ASSERT_TRUE(first_index.has_value());
  ASSERT_TRUE(second_index.has_value());

  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*first_index, 0U);

  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_EQ(*second_index, 0U);
}

TEST(ThisThreadTest, ConcurrentWorkersReportDistinctIndices) {
  constexpr std::size_t worker_count = 4;

  crayon::ThreadPool pool{worker_count};

  std::mutex mutex;
  std::condition_variable condition;
  std::size_t arrived = 0;
  bool release = false;

  std::vector<std::future<std::size_t>> futures;
  futures.reserve(worker_count);

  for (std::size_t task_index = 0; task_index < worker_count; ++task_index) {
    futures.push_back(pool.submit([&] {
      std::optional<std::size_t> const index =
          crayon::this_thread::get_pool_index();

      EXPECT_TRUE(crayon::this_thread::is_pool_thread());
      EXPECT_EQ(crayon::this_thread::get_pool(), &pool);
      EXPECT_TRUE(index.has_value());

      {
        std::unique_lock lock{mutex};

        ++arrived;
        condition.notify_all();

        condition.wait(lock, [&release] { return release; });
      }

      return index.value_or(worker_count);
    }));
  }

  {
    std::unique_lock lock{mutex};

    bool const all_workers_arrived = condition.wait_for(
        lock, 5s, [&arrived] { return arrived == worker_count; });

    if (!all_workers_arrived) {
      release = true;
      lock.unlock();
      condition.notify_all();

      FAIL() << "Not all workers began executing concurrently";
    }

    release = true;
  }

  condition.notify_all();

  std::set<std::size_t> indices;

  for (auto &future : futures) {
    indices.insert(future.get());
  }

  EXPECT_EQ(indices.size(), worker_count);

  for (std::size_t index = 0; index < worker_count; ++index) {
    EXPECT_TRUE(indices.contains(index));
  }
}

TEST(ThisThreadTest, ContextValuesAreConsistentInsideTask) {
  crayon::ThreadPool pool{2};

  struct Result {
    bool is_pool_thread;
    crayon::ThreadPool *pool;
    std::optional<std::size_t> pool_index;
    bool stop_possible;
  };

  auto future = pool.submit([] {
    std::stop_token const token = crayon::this_thread::get_stop_token();

    return Result{
        .is_pool_thread = crayon::this_thread::is_pool_thread(),
        .pool = crayon::this_thread::get_pool(),
        .pool_index = crayon::this_thread::get_pool_index(),
        .stop_possible = token.stop_possible(),
    };
  });

  Result const result = future.get();

  EXPECT_TRUE(result.is_pool_thread);
  EXPECT_EQ(result.pool, &pool);
  EXPECT_TRUE(result.pool_index.has_value());

  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  EXPECT_LT(*result.pool_index, 2U);
  EXPECT_TRUE(result.stop_possible);
}

TEST(ThisThreadTest, CallingPublicAccessorsDoesNotChangeContext) {
  crayon::ThreadPool pool{1};

  auto future = pool.submit([] {
    crayon::ThreadPool *const initial_pool = crayon::this_thread::get_pool();

    auto const initial_index = crayon::this_thread::get_pool_index();

    std::stop_token const initial_token = crayon::this_thread::get_stop_token();

    for (int iteration = 0; iteration < 100; ++iteration) {
      EXPECT_TRUE(crayon::this_thread::is_pool_thread());
      EXPECT_EQ(crayon::this_thread::get_pool(), initial_pool);
      EXPECT_EQ(crayon::this_thread::get_pool_index(), initial_index);

      std::stop_token const token = crayon::this_thread::get_stop_token();

      EXPECT_EQ(token.stop_possible(), initial_token.stop_possible());
      EXPECT_EQ(token.stop_requested(), initial_token.stop_requested());
    }
  });

  future.get();
}

TEST(ThisThreadTest, MainThreadRemainsOutsidePoolWhileTaskRuns) {
  crayon::ThreadPool pool{1};

  std::promise<void> started_promise;
  std::future<void> started_future = started_promise.get_future();

  std::promise<void> release_promise;
  std::shared_future<void> release_future =
      release_promise.get_future().share();

  auto task = pool.submit([&started_promise, release_future] {
    EXPECT_TRUE(crayon::this_thread::is_pool_thread());
    started_promise.set_value();
    release_future.wait();
  });

  started_future.wait();

  EXPECT_FALSE(crayon::this_thread::is_pool_thread());
  EXPECT_EQ(crayon::this_thread::get_pool(), nullptr);
  EXPECT_EQ(crayon::this_thread::get_pool_index(), std::nullopt);
  EXPECT_FALSE(crayon::this_thread::get_stop_token().stop_possible());

  release_promise.set_value();
  task.get();
}

TEST(ThisThreadTest, ContextDoesNotLeakIntoUnrelatedThread) {
  crayon::ThreadPool pool{1};

  auto pool_task = pool.submit([] {
    EXPECT_TRUE(crayon::this_thread::is_pool_thread());

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();

    std::jthread unrelated_thread{[&promise] {
      bool const context_is_empty =
          !crayon::this_thread::is_pool_thread() &&
          crayon::this_thread::get_pool() == nullptr &&
          crayon::this_thread::get_pool_index() == std::nullopt &&
          !crayon::this_thread::get_stop_token().stop_possible();

      promise.set_value(context_is_empty);
    }};

    return future.get();
  });

  EXPECT_TRUE(pool_task.get());
}

TEST(ThisThreadTest, MainThreadContextRemainsEmptyAfterPoolDestruction) {
  {
    crayon::ThreadPool pool{2};

    auto future = pool.submit([] {
      EXPECT_TRUE(crayon::this_thread::is_pool_thread());
      EXPECT_NE(crayon::this_thread::get_pool(), nullptr);
    });

    future.get();
  }

  EXPECT_FALSE(crayon::this_thread::is_pool_thread());
  EXPECT_EQ(crayon::this_thread::get_pool(), nullptr);
  EXPECT_EQ(crayon::this_thread::get_pool_index(), std::nullopt);
  EXPECT_FALSE(crayon::this_thread::get_stop_token().stop_possible());
}

} // namespace
