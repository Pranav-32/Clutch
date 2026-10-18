/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Stage Windows DLL dependencies beside executable/test artifacts.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <utility>
#include <vector>

namespace clutch {
struct Graph;
struct Options;
/// Return library-index/destination pairs, including private runtime
/// dependencies. POSIX generators use embedded relative runtime search paths
/// instead.
std::vector<std::pair<size_t, fs::path>> runtime_copies(const Graph &graph,
                                                        size_t consumer);
/// Native backend action for one declared DLL dependency; never modifies PATH.
std::vector<std::string> runtime_copy_command(const Graph &graph,
                                              const Options &options,
                                              const fs::path &executable,
                                              size_t consumer,
                                              size_t library);
/// Copy one reachable DLL to its validated destination; reject arbitrary names.
void copy_runtime_library(const Graph &graph,
                          size_t consumer,
                          const std::string &library);
}  // namespace clutch
