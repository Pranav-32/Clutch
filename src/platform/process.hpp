/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Find executable programs and launch native Windows or
 * POSIX child processes.
 */

#pragma once

#include "core/paths.hpp"

#include <string>
#include <vector>

namespace clutch {
/// Locate the running executable using the Windows module path, or resolve the
/// invocation through the host's executable search rules on POSIX.
fs::path current_executable(const std::string &invocation);
/// Resolve an executable name through PATH, or validate an explicit executable
/// path. Return an empty string when no candidate is usable.
std::string find_program(const std::string &name);
/// Launch a native child process in a working directory and wait for its exit
/// status.
/// Inherits environment and standard streams; throws when process setup fails.
/// POSIX child-side directory/exec failures return 126/127; signal termination
/// returns 128 plus the signal number. The parent's working directory is
/// unchanged.
int run(const std::vector<std::string> &args, const fs::path &directory);
}  // namespace clutch
