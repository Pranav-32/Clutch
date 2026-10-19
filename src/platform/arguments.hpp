/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Keep shell command quoting separate from compiler response encoding.
 */
#pragma once

#include <string>
#include <vector>

namespace clutch {
/// Serialize Windows CRT arguments or POSIX-shell recipes. POSIX execution
/// itself passes argv directly and does not use shell quoting.
std::string command_line(const std::vector<std::string> &arguments);

/// Encode the arguments after command[0] using the selected driver's grammar.
/// Microsoft tools receive a UTF-8 BOM; GNU-style tools receive plain UTF-8.
/// The result stays on one line so Ninja can embed it in rspfile_content.
std::string response_file(const std::vector<std::string> &command, bool msvc);
}  // namespace clutch
