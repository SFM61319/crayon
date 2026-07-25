// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <string>

#include <gtest/gtest.h>

#include <crayon/crayon.hpp>

TEST(CrayonHello, HelloCrayon) {
  std::string const expected{"Hello, Crayon!"};
  auto const actual{crayon::hello_crayon()};

  EXPECT_EQ(actual, expected);
}
