// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <crayon/crayon.hpp>

namespace {

TEST(CrayonHeaderTest, ExposesThreadPool) {
  crayon::ThreadPool pool{1};
  auto future{pool.submit([] { return 42; })};

  EXPECT_EQ(future.get(), 42);
}

} // namespace
