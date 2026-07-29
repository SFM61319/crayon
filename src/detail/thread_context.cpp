// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include "detail/thread_context.hpp"

namespace crayon::detail {

ThreadContextGuard::ThreadContextGuard(ThreadContext &context) noexcept
    : previous_thread_context_{current_thread_context} {
  current_thread_context = &context;
}

ThreadContextGuard::~ThreadContextGuard() noexcept {
  current_thread_context = previous_thread_context_;
}

} // namespace crayon::detail
