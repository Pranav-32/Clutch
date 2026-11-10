/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Capture bounded native-tool output without invoking a command shell.
 */
#pragma once

#include "core/paths.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace clutch {
struct ProcessOutput {
  int status;
  std::string text;
};

/// Capture stdout and inherit stderr; drain the pipe before waiting so large
/// output cannot deadlock. Optionally merge stderr for private health probes.
/// A nonzero timeout runs the child in its own job (Windows) or process group
/// (POSIX, with stdin from /dev/null); on expiry it terminates that whole tree,
/// reaps the direct child and throws. A child that exits in time returns even
/// if helpers it started still hold the pipe; those helpers are left running.
/// Throws on setup/read errors or oversized output.
ProcessOutput capture(const std::vector<std::string> &arguments,
                      const fs::path &directory,
                      bool merge_stderr = false,
                      std::chrono::milliseconds timeout = {});
}  // namespace clutch
