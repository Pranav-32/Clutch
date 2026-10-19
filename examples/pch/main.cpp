/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Print the result after linking two prefix-header consumers.
 */
#include "prefix.hpp"
#include "sum.hpp"

/// Run the example and report a failed calculation through its exit status.
int main() {
  const auto result = sum();
  std::cout << result << '\n';
  return result == 42 ? 0 : 1;
}
