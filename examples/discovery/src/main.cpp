/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Link discovered C and nested C++ sources without listing an entry.
 */
#include "detail/greeting.hpp"
#include "math.h"
#include <iostream>

int main() {
  std::cout << example_greeting() << ": " << example_multiply(6, 7) << '\n';
}
