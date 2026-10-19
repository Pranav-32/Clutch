/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Parse real command-line arguments with a downloaded CLI11 release.
 */
#pragma once
#include <CLI/CLI.hpp>
#include <array>
#include <iostream>

namespace clutch_example {
// Published sample argument used by the package's independent consumer test.
inline constexpr auto expected_answer = 42;

/// Exercise CLI11 itself so the test checks the resolved package's interface.
inline bool verify() {
  CLI::App app("Dependency check");
  auto value = 0;
  app.add_option("--value", value)->required();
  constexpr std::array arguments{"package-check", "--value", "42"};
  app.parse(static_cast<int>(arguments.size()), arguments.data());
  return value == expected_answer;
}

/// Forward application options and retain CLI11's normal help/error handling.
inline int run(const int argc, char **const argv) {
  CLI::App app("Clutch GitHub header-only example");
  auto value = expected_answer;
  app.add_option("--value", value, "Value to print");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    return app.exit(error);
  }

  std::cout << "GitHub CLI11: " << value << '\n';
  return 0;
}
}  // namespace clutch_example
