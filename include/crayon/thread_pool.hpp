// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace crayon {

/// @brief Represents a user-created <a
/// href="https://en.wikipedia.org/wiki/Thread_pool" target="_blank">thread
/// pool</a>.
///
/// You can execute functions explicitly within this @ref ThreadPool using
/// @ref ThreadPool::submit. @ref ThreadPool::submit executes a closure in one
/// of the @ref ThreadPool "ThreadPool's" threads.
///
/// When this @ref ThreadPool is destructed, that's a signal for the threads it
/// manages to terminate, they will complete executing any remaining work that
/// you have spawned, and automatically terminate.
///
/// @par Thread safety
/// @ref ThreadPool::submit, @ref ThreadPool::try_submit, @ref
/// ThreadPool::shutdown(), and @ref ThreadPool::is_shutting_down may be called
/// concurrently while this object's lifetime remains active.
///
/// Blocking shutdown using @ref ThreadPool::shutdown(bool) with `wait == true`
/// must not execute concurrently with another blocking shutdown or with
/// destruction.
///
/// Destruction must not race with any operation on this object. Blocking
/// shutdown and destruction must not be initiated from a worker thread owned by
/// this same pool.
///
/// @par Task execution
/// Tasks may submit additional tasks to this pool. However, tasks should not
/// synchronously wait for futures produced by this same pool, as this can
/// exhaust all workers and deadlock.
///
/// Submitted functions and arguments are decay-copied into task storage.
/// References must be passed explicitly with @ref std::ref and must remain
/// valid until task execution completes.
///
/// Shutdown is cooperative. It does not forcibly interrupt executing user code,
/// so shutdown may block indefinitely if a task never returns.
class ThreadPool final {
public:
  /// @brief Represents the result of a successful invocation of the @p Function
  /// with its @p Args.
  ///
  /// Requires the @p Function to be invocable with @p Args.
  ///
  /// @tparam Function Type of the function called with the @p Args.
  /// @tparam Args Type of the arguments passed to the @p Function.
  template <typename Function, typename... Args>
    requires std::invocable<std::decay_t<Function>, std::decay_t<Args>...>
  using result_t =
      std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>;

  /// @brief Returns an estimate of the default amount of parallelism a program
  /// should use.
  ///
  /// Parallelism is a resource. A given machine provides a certain capacity for
  /// parallelism, i.e., a bound on the number of computations it can perform
  /// simultaneously. This number often corresponds to the amount of CPUs a
  /// computer has, but it may diverge in various cases.
  ///
  /// Host environments such as VMs or container orchestrators may want to
  /// restrict the amount of parallelism made available to programs in them.
  /// This is often done to limit the potential impact of (unintentionally)
  /// resource-intensive programs on other programs running on the same machine.
  ///
  /// Guaranteed to return a non-zero value.
  ///
  /// @return An estimate of the default amount of parallelism a program should
  /// use.
  static std::size_t available_parallelism() noexcept;

  /// @brief Constructs a new thread pool with @ref
  /// ThreadPool::available_parallelism "the available amount of parallelism".
  ThreadPool();

  /// @brief Constructs a new thread pool with the given number of threads.
  ///
  /// If the given @p num_threads is zero, this uses @ref
  /// ThreadPool::available_parallelism "the available amount of parallelism".
  ///
  /// @param num_threads The number of threads to create in the thread pool.
  explicit ThreadPool(std::size_t num_threads);

  /// @cond
  // A ThreadPool cannot safely be copied or moved because its worker threads
  // reference synchronization state owned by this specific object.
  ThreadPool(ThreadPool const &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool const &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
  /// @endcond

  /// @brief Returns the (current) number of threads in the thread pool.
  ///
  /// @return The (current) number of threads in the thread pool.
  [[nodiscard]]
  std::size_t current_num_threads() const;

  /// @brief Returns whether this @ref ThreadPool is shutting down.
  ///
  /// A @ref ThreadPool starts shutting down when explicitly requested by the
  /// owner or implicitly destroyed by the compiler at the end of the scope. It
  /// shuts down fully only after all tasks have been drained and executed by
  /// the now-dormant worker threads.
  ///
  /// @return Whether this @ref ThreadPool is shutting down.
  [[nodiscard]]
  bool is_shutting_down() const;

  /// @brief Tries to enqueue and submit a function and its arguments for
  /// execution in a background worker thread in this @ref ThreadPool, returns
  /// a @ref std::future "future" result on successful submission, and an @ref
  /// std::nullopt "empty" value otherwise.
  ///
  /// Requires the @p function to be invocable with @p args.
  ///
  /// An enqueue and submission attempt may fail due to at least one of the
  /// following reasons:
  ///   - The @ref ThreadPool @ref ThreadPool::is_shutting_down "is shutting
  ///   down".
  ///
  /// A failed submission may still consume functions or arguments passed as
  /// rvalues because task storage is constructed before the pool determines
  /// whether it can accept the task.
  ///
  /// An empty result specifically indicates that the pool rejected the task
  /// because shutdown had begun. Other failures, including allocation failure
  /// or exceptions thrown while copying or moving the function or arguments,
  /// are propagated to the caller.
  ///
  /// For a simpler, unwrapped alternative, @see ThreadPool::submit.
  ///
  /// @tparam Function Type of the @p function to be called with the @p args.
  /// @tparam Args Type of the @p args to be passed to the @p function.
  ///
  /// @param function The function to be called with the @p args after
  /// submission.
  /// @param args The arguments to be passed to the @p function after
  /// submission.
  ///
  /// @return A @ref std::future "future" result on successful submission, and
  /// an @ref std::nullopt "empty" value otherwise.
  template <typename Function, typename... Args>
    requires std::invocable<std::decay_t<Function>, std::decay_t<Args>...>
  [[nodiscard]]
  std::optional<std::future<result_t<Function, Args...>>>
  try_submit(Function &&function, Args &&...args) {
    // `std::packaged_task` stores the result or exception in shared state that
    // the returned future can observe.
    // `std::move_only_function` is used instead of `std::function` because
    // `std::packaged_task` is move-only.
    auto task{package_task(std::forward<Function>(function),
                           std::forward<Args>(args)...)};

    auto future{task.get_future()};
    if (try_enqueue_task_and_notify_one(std::move(task))) {
      return future;
    }

    return std::nullopt;
  }

  /// @brief Enqueues and submits a function and its arguments for execution in
  /// a background worker thread in this @ref ThreadPool, returns a @ref
  /// std::future "future" result on successful submission, and failing
  /// otherwise.
  ///
  /// Requires the @p function to be invocable with @p args.
  ///
  /// An enqueue and submission attempt may fail due to at least one of the
  /// following reasons:
  ///   - The @ref ThreadPool @ref ThreadPool::is_shutting_down "is shutting
  ///   down".
  ///
  /// A rejected submission may still consume functions or arguments passed as
  /// rvalues because task storage is constructed before the pool determines
  /// whether it can accept the task.
  ///
  /// To handle shutdown rejection without an exception, @see
  /// ThreadPool::try_submit.
  ///
  /// @tparam Function Type of the @p function to be called with the @p args.
  /// @tparam Args Type of the @p args to be passed to the @p function.
  ///
  /// @param function The function to be called with the @p args after
  /// submission.
  /// @param args The arguments to be passed to the @p function after
  /// submission.
  ///
  /// @return A @ref std::future "future" result on successful submission.
  /// @throw std::runtime_error If shutdown has begun and the pool no longer
  /// accepts tasks.
  /// @throw Any exception propagated while constructing or enqueueing the task,
  /// including allocation failures and exceptions thrown while copying or
  /// moving the function or arguments.
  template <typename Function, typename... Args>
    requires std::invocable<std::decay_t<Function>, std::decay_t<Args>...>
  [[nodiscard]]
  std::future<result_t<Function, Args...>> submit(Function &&function,
                                                  Args &&...args) {
    if (auto maybe_future{try_submit(std::forward<Function>(function),
                                     std::forward<Args>(args)...)}) {
      return std::move(*maybe_future);
    }

    throw std::runtime_error(
        "crayon::ThreadPool is shutting down and not accepting new tasks");
  }

  /// @brief Requests this @ref ThreadPool to gracefully shutdown after draining
  /// and executing all the queued and inflight tasks, respectively, disallows
  /// future @ref ThreadPool::submit "task submissions", and returns immediately
  /// without waiting for this @ref ThreadPool to fully shutdown.
  ///
  /// To explicitly control whether to wait until complete shutdown, use @ref
  /// ThreadPool::shutdown(bool).
  ///
  /// This overload is thread-safe and may be called concurrently with task
  /// submission and with other non-blocking shutdown requests, provided that
  /// this object's lifetime remains active.
  ///
  /// This is an idempotent, irreversible operation. Once shutdown is
  /// successfully requested, this @ref ThreadPool can no longer be used to
  /// process tasks. Unless you absolutely need to explicitly request shutdown
  /// before the end of the scope is reached, prefer letting the compiler shut
  /// this @ref ThreadPool down on destruction at the end of the scope.
  void shutdown() noexcept;

  /// @brief Requests this @ref ThreadPool to gracefully shutdown after draining
  /// and executing all the queued and inflight tasks, respectively, disallows
  /// future @ref ThreadPool::submit "task submissions", and only waits for this
  /// @ref ThreadPool to fully shutdown if @p wait is explicitly requested.
  ///
  /// When @p wait is `false`, this operation is thread-safe and may be called
  /// concurrently with task submission and other non-blocking shutdown
  /// requests while this object's lifetime remains active.
  ///
  /// When @p wait is `true`, the caller must ensure that no other blocking
  /// shutdown or destruction executes concurrently. It must also not be called
  /// from a worker thread owned by this same pool.
  ///
  /// Waiting is cooperative. This function may block indefinitely if an
  /// executing task never returns or waits on work that cannot make progress.
  ///
  /// This is an idempotent, irreversible operation. Once shutdown is
  /// successfully requested, this @ref ThreadPool can no longer be used to
  /// process tasks. Unless you absolutely need to explicitly request shutdown
  /// before the end of the scope is reached, prefer letting the compiler shut
  /// this @ref ThreadPool down on destruction at the end of the scope.
  ///
  /// @param wait Whether to wait until every worker has finished and been
  /// joined.
  void shutdown(bool wait) noexcept;

  /// @brief Gracefully shuts down this pool and joins all worker threads.
  ///
  /// Queued tasks are drained and executing tasks are allowed to finish before
  /// destruction completes.
  ///
  /// The caller must ensure that no other thread accesses this object once
  /// destruction begins. Destruction must not be initiated from a worker thread
  /// owned by this same pool.
  ~ThreadPool() noexcept;

private:
  /// @brief Represents a nullarized packaged task.
  ///
  /// A nullarized packaged task is a packaged task that requires zero arguments
  /// to allow arbitrary task signatures to be submitted, as it stores the
  /// invocation argument mapping internally.
  ///
  /// @sa ThreadPool::package_task
  using task_t = std::move_only_function<void()>;

  /// @brief The synchronization primitive to allow mutually exclusive
  /// thread-safe access to shared resources.
  mutable std::mutex mutex_{};

  /// @brief The synchronization primitive to efficiently block a thread and put
  /// it to sleep until a state change triggers an event notification that makes
  /// a waiting condition true.
  std::condition_variable_any condition_{};

  /// @brief Whether this @ref ThreadPool is shutting down.
  ///
  /// A @ref ThreadPool starts shutting down when explicitly requested by the
  /// owner or implicitly destroyed by the compiler at the end of the scope. It
  /// shuts down fully only after all tasks have been drained and executed by
  /// the now-dormant worker threads.
  bool is_shutting_down_{};

  /// @brief The queue of all the nullarized tasks submitted to this @ref
  /// ThreadPool, shared by all the @ref ThreadPool::workers_ "worker threads".
  std::queue<task_t> tasks_{};

  /// @brief The list of all the worker threads created to execute nullary tasks
  /// in parallel indefinitely (until shutdown is requested).
  std::vector<std::jthread> workers_{};

  /// @brief Tries to enqueue a nullarized @p task, returns `true` on success,
  /// and `false` otherwise.
  ///
  /// An enqueue attempt may fail due to at least one of the following reasons:
  ///   - The @ref ThreadPool @ref ThreadPool::is_shutting_down "is shutting
  ///   down".
  ///
  /// This *only* enqueues the tasks, but does not notify the worker threads.
  /// If you want the worker threads to execute the task, @ref
  /// ThreadPool::try_enqueue_task_and_notify_one "try to enqueue the task and
  /// notify one thread" along with it.
  ///
  /// @param task The nullarized task to try to enqueue.
  ///
  /// @return `true` on success, and `false` otherwise.
  bool try_enqueue_task(task_t &&task);

  /// @brief Tries to enqueue a nullarized @p task, notifies exactly one worker
  /// thread only on successful enqueue, returns `true` on success, and `false`
  /// otherwise.
  ///
  /// An enqueue attempt may fail due to at least one of the following reasons:
  ///   - The @ref ThreadPool @ref ThreadPool::is_shutting_down "is shutting
  ///   down".
  ///
  /// This enqueues the task *and* notifies exactly one worker thread. If you
  /// want to enqueue without notifying worker threads, @ref
  /// ThreadPool::try_enqueue_task "try to enqueue the task only" without
  /// notifying workers threads.
  ///
  /// @param task The nullarized task to try to enqueue.
  ///
  /// @return `true` on success, and `false` otherwise.
  bool try_enqueue_task_and_notify_one(task_t &&task);

  /// @brief Tries to pop a nullarized task, blocking the thread and waiting for
  /// tasks while sleeping if the @ref ThreadPool::tasks_ "task queue" is empty,
  /// and returns the task on success and an @ref std::nullopt "empty" value on
  /// failure.
  ///
  /// A pop attempt may fail due to at least one of the following reasons:
  ///   - The @ref ThreadPool::tasks_ "task queue" is empty and the @ref
  ///   ThreadPool @ref ThreadPool::is_shutting_down "is shutting down".
  ///
  /// A stop request wakes the worker but does not discard queued tasks. The
  /// worker continues draining available tasks before terminating.
  ///
  /// @param stop_token The @ref std::stop_token "stop token" passed by the @ref
  /// std::jthread "thread" to notify on stop requests.
  ///
  /// @return The nullarized task on success and an @ref std::nullopt "empty"
  /// value on failure.
  std::optional<task_t> try_wait_and_pop_task(std::stop_token stop_token);

  /// @brief The loop each worker thread executes until stopped.
  ///
  /// @param stop_token The @ref std::stop_token "stop token" passed by the @ref
  /// std::jthread "thread" to notify on stop requests.
  void worker_loop(std::stop_token stop_token);

  /// @brief Tries to shutdown this @ref ThreadPool, returns `true` on
  /// success, and `false` otherwise.
  ///
  /// A shutdown attempt may fail due to at least one of the following reasons:
  ///   - Shutdown was previously attempted successfully.
  ///
  /// This *only* requests a shutdown, but does not notify the worker threads.
  /// If you want the worker threads to listen to this request, @ref
  /// ThreadPool::try_shutdown_and_notify_all "try to shutdown and notify all
  /// threads" along with it.
  ///
  /// @return `true` on success, and `false` otherwise.
  bool try_shutdown() noexcept;

  /// @brief Tries to shutdown this @ref ThreadPool, notifies all worker threads
  /// only on successful shutdown request, returns `true` on success, and
  /// `false` otherwise.
  ///
  /// A shutdown attempt may fail due to at least one of the following reasons:
  ///   - Shutdown was previously attempted successfully.
  ///
  /// This requests a shutdown *and* notifies all worker threads. If you
  /// want to request a shutdown without notifying worker threads, @ref
  /// ThreadPool::try_shutdown "try to request a shutdown only" without
  /// notifying workers threads.
  ///
  /// @return `true` on success, and `false` otherwise.
  bool try_shutdown_and_notify_all() noexcept;

  /// @brief @ref std::packaged_task "Packages" any arbitrary @p function with
  /// the @p args mapped for nullary invocation and @ref std::promise "promised"
  /// notification in the @ref std::future "future" and returns it.
  ///
  /// The function and arguments are decay-copied into the packaged task and are
  /// invoked as rvalues. To pass a reference, use @ref std::ref or
  /// @ref std::cref and ensure the referenced object outlives task execution.
  ///
  /// @tparam Function Type of the @p function to be called with the @p args.
  /// @tparam Args Type of the @p args to be passed to the @p function.
  ///
  /// @param function The function to be called with the @p args.
  /// @param args The arguments to be passed to the @p function.
  ///
  /// @return A packaged nullarized task for future invocation and notification.
  template <typename Function, typename... Args>
    requires std::invocable<std::decay_t<Function>, std::decay_t<Args>...>
  [[nodiscard]]
  static std::packaged_task<result_t<Function, Args...>()>
  package_task(Function &&function, Args &&...args) {
    using Result = result_t<Function, Args...>;
    return std::packaged_task<Result()>{
        // Workers consume homogeneous `void()` tasks, so bind the callable and
        // its arguments into a nullary operation.
        [function{std::forward<Function>(function)},
         ... args{std::forward<Args>(args)}]() mutable -> Result {
          return std::invoke(std::move(function), std::move(args)...);
        }};
  }
};

} // namespace crayon
