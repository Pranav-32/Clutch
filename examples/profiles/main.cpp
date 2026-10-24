/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief A warning-clean application usable with independent profile policies.
 */
#include <array>
#include <iostream>
#include <numeric>

int main() {
  constexpr std::array values{12, 14, 16};
  std::cout << "Profile example: "
            << std::accumulate(values.begin(), values.end(), 0) << '\n';
}
