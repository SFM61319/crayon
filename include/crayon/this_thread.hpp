// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <stop_token>

#include <crayon/thread_pool.hpp>

namespace crayon::this_thread {

/// @brief Returns whether the calling thread is a @ref ThreadPool thread.
///
/// @return `true` if the calling thread is currently executing as a @ref
/// ThreadPool thread, `false` otherwise.
[[nodiscard]]
bool is_pool_thread() noexcept;

/// @brief Returns the @ref ThreadPool associated with the calling thread.
///
/// @note The returned pointer is valid only while the associated @ref
/// ThreadPool remains alive.
///
/// @return A non-owning pointer to the current @ref ThreadPool, or @ref nullptr
/// if the calling thread is not a pool thread.
[[nodiscard]]
ThreadPool *get_pool() noexcept;

/// @brief Returns the zero-based index of the calling thread.
///
/// @note The returned index is stable for the lifetime of the thread.
///
/// @return The thread index if the calling thread is a pool thread, @ref
/// std::nullopt otherwise.
[[nodiscard]]
std::optional<std::size_t> get_pool_index() noexcept;

/// @brief Returns the stop token associated with the calling thread.
///
/// @return The current thread's @ref std::stop_token. If the calling thread is
/// not a pool thread, returns a default-constructed token for which
/// @ref std::stop_token::stop_possible() is @ref false.
[[nodiscard]]
std::stop_token get_stop_token() noexcept;

} // namespace crayon::this_thread
