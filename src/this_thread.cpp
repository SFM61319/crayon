// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <optional>
#include <stop_token>

#include <crayon/this_thread.hpp>
#include <crayon/thread_pool.hpp>

#include "detail/thread_context.hpp"

namespace crayon::this_thread {

bool is_pool_thread() noexcept {
  return detail::current_thread_context != nullptr;
}

ThreadPool *get_pool() noexcept {
  return is_pool_thread() ? detail::current_thread_context->pool : nullptr;
}

std::optional<std::size_t> get_pool_index() noexcept {
  return is_pool_thread() ? detail::current_thread_context->pool_index
                          : std::nullopt;
}

std::stop_token get_stop_token() noexcept {
  return is_pool_thread() ? detail::current_thread_context->stop_token
                          : std::stop_token{};
}

} // namespace crayon::this_thread
