/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Capture bounded native-tool output without invoking a command shell.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
struct ProcessOutput {
  int status;
  std::string text;
};

/// Capture stdout and inherit stderr; drain the pipe before waiting so large
/// output cannot deadlock. Throws on setup/read errors or oversized output.
ProcessOutput capture(const std::vector<std::string> &arguments,
                      const fs::path &directory);
}  // namespace clutch
