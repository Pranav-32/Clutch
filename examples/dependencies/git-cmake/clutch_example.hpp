/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Consume fmt's compiled CMake target through a pinned Git import.
 */
#pragma once
#include <fmt/format.h>

namespace clutch_example {
// Stable output makes the package test exercise fmt's actual formatting API.
inline constexpr auto expected_answer = 42;

/// Compiling and running this call requires fmt's public includes and library.
inline bool verify() {
  return fmt::format("Package answer: {}", expected_answer) ==
         "Package answer: 42";
}

/// Clutch prepares the upstream build before linking this consumer.
inline int run(int, char **) {
  fmt::print("Git CMake fmt: {}\n", expected_answer);
  return verify() ? 0 : 1;
}
}  // namespace clutch_example
