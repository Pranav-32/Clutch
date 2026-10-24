/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Native adapters for optional formatter, analyzer and debugger tools.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace clutch {
struct Options;
struct Graph;
/// Format owned sources/headers without configuring or downloading
/// dependencies.
int format_project(const Options &options, const std::string &tool, bool check);
/// Analyze configured project translation units with their real compiler flags.
int lint_project(const Options &options,
                 const std::string &tool,
                 const std::string &checks);
/// Launch an interactive debugger for the selected, already built executable.
int debug_target(const Graph &graph,
                 size_t target,
                 const std::string &tool,
                 const std::vector<std::string> &arguments);
}  // namespace clutch
