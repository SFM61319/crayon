// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <iostream>

#include <crayon/crayon.hpp>

int main() {
  auto const greeting{crayon::hello_crayon()};
  std::cout << greeting << '\n';

  return 0;
}
