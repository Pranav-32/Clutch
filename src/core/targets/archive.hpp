/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Recreate static archives portably without retaining removed object
 * members.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
struct Graph;
struct Options;
/// Internal backend action: validate a static target and execute its archiver
/// with a response file (Apple libtool uses a file list).
int archive_target(const Graph &graph, size_t index);
/// Native rules call Clutch itself; no Python, CMake, rm or scripting runtime.
std::vector<std::string> archive_command(const Graph &graph,
                                         const Options &options,
                                         const fs::path &executable,
                                         size_t index);
}  // namespace clutch
