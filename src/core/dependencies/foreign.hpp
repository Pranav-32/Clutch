/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Adapt native package interfaces without taking over their build graph.
 */
#pragma once
#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
struct Graph;
struct Options;
struct Project;
struct Toolchain;
struct BuildBackend;
}  // namespace clutch

namespace clutch::dependencies {
/// Configure foreign libraries with the exact consuming compiler/profile.
void configure_foreign(Project &project,
                       const Toolchain &tools,
                       const BuildBackend &backend,
                       const Options &options);
/// Build only foreign actions reachable from the selected native targets.
int build_foreign(const Graph &graph,
                  const Options &options,
                  const std::vector<size_t> &selected);
/// Execute one saved action for a generated native graph and update its
/// interface stamp only when artifact/header timestamps or membership change.
int prepare_foreign(const Graph &graph,
                    const Options &options,
                    const std::string &id);
/// Stable prerequisite shared by every consumer of a foreign action.
fs::path foreign_stamp(const Graph &graph, const std::string &id);
/// Stage explicitly discovered DLLs beside the selected executables.
void stage_foreign_runtime(const Graph &graph,
                           const std::vector<size_t> &selected);
}  // namespace clutch::dependencies
