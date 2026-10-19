/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Consume a versioned native package fetched through Git.
 */
#pragma once
#include <iostream>
#include <number.hpp>

namespace clutch_example {
/// Verify the sample's published library interface after checkout and linking.
inline bool verify() {
  constexpr auto expected_answer = 42;
  return demo_number::answer() == expected_answer;
}

/// A local Git origin uses the same resolver and cache as a hosted repository.
inline int run(int, char **) {
  std::cout << "Git package: " << demo_number::answer() << '\n';
  return verify() ? 0 : 1;
}
}  // namespace clutch_example
