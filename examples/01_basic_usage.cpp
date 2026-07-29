// Copyright (c) 2026 Avinash Maddikonda
// SPDX-License-Identifier: Apache-2.0

#include <iostream>

#include <crayon/crayon.hpp>

int main() {
  auto const
      answer_to_the_ultimate_question_of_life_the_universe_and_everything{42};
  crayon::ThreadPool pool{1};

  auto maybe_future{pool.try_submit([] {
    return answer_to_the_ultimate_question_of_life_the_universe_and_everything;
  })};
  if (!maybe_future) {
    // NOLINTNEXTLINE(performance-avoid-endl)
    std::cout << "Houston, we have a problem" << std::endl;
    return 1;
  }

  auto const result{
      maybe_future->get() -
      answer_to_the_ultimate_question_of_life_the_universe_and_everything};

  // NOLINTNEXTLINE(performance-avoid-endl)
  std::cout << "Great Success! (" << result << ')' << std::endl;
  return result;
}
