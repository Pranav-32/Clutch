/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Share native dependency callbacks across build-file generators.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
struct Graph;
struct Options;
/// Native action nodes occupy one executor slot; upstream compilation gets one
/// job so independent scheduler pools cannot multiply the outer job budget.
std::vector<std::string> foreign_action_command(const Graph &graph,
                                                const Options &options,
                                                const fs::path &executable,
                                                const std::string &id);
/// Stage imported DLLs after the selected executable's upstream preparation.
std::vector<std::string> foreign_runtime_command(const Graph &graph,
                                                 const Options &options,
                                                 const fs::path &executable,
                                                 size_t target);
}  // namespace clutch
