// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <stop_token>

#include <crayon/thread_pool.hpp>

namespace crayon::detail {

/// @brief The execution context associated with a @ref ThreadPool thread.
///
/// Instances are installed in thread-local storage (TLS) while a thread is
/// executing. They provide the backing state for the @ref crayon::this_thread
/// API.
struct ThreadContext {
  /// @brief The associated @ref ThreadPool.
  ThreadPool *pool{};

  /// @brief The zero-based index of the thread.
  std::optional<std::size_t> pool_index{};

  /// @brief The thread's stop token.
  std::stop_token stop_token{};
};

/// @brief The thread-local execution context for the calling thread.
///
/// A value of @ref nullptr indicates that the calling thread is not currently
/// executing as a @ref ThreadPool thread.
inline thread_local ThreadContext *current_thread_context{};

/// @brief Installs a @ref ThreadContext for the lifetime of the guard.
///
/// Upon construction, the supplied context becomes the current thread context.
/// Upon destruction, the previously active context is restored.
class ThreadContextGuard final {
public:
  /// @brief Installs the specified thread context.
  ///
  /// @param context The context to install for the current thread.
  explicit ThreadContextGuard(ThreadContext &context) noexcept;

  ThreadContextGuard(ThreadContextGuard const &) = delete;
  ThreadContextGuard(ThreadContextGuard &&) = delete;
  ThreadContextGuard &operator=(ThreadContextGuard const &) = delete;
  ThreadContextGuard &operator=(ThreadContextGuard &&) = delete;

  /// @brief Restores the previously active thread context.
  ~ThreadContextGuard() noexcept;

private:
  /// @brief The previously active thread context.
  ThreadContext *previous_thread_context_;
};

} // namespace crayon::detail
