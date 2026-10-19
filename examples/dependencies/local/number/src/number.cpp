/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Implement a compiled dependency independently of its consumers.
 */
#include "number.hpp"

namespace demo_number {
int answer() {
  // A stable sample result lets examples detect stale or missing library code.
  constexpr auto sample_answer = 42;
  return sample_answer;
}
}  // namespace demo_number
