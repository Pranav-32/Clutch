/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Resolve usage propagation, library link order and named selections.
 */
#pragma once

#include "core/targets/target.hpp"

namespace clutch {
struct Project;
struct Graph;

struct ResolvedTarget {
  TargetSpec spec;
  std::vector<size_t> dependencies, link_dependencies;
  bool needs_cpp = false;
};

/// Normalize legacy applications/tests and explicit targets, rejecting missing,
/// executable and circular links before generating commands.
std::vector<ResolvedTarget> resolve_targets(const Project &project);
/// Choose a target's platform-native filename inside the profile build
/// directory.
fs::path
target_output(const Project &project, const TargetSpec &spec, bool msvc);
/// Use the import library for Windows shared targets and the artifact
/// elsewhere.
fs::path
target_import_library(const fs::path &output, TargetType type, bool msvc);
/// Choose default build targets, test targets or exact user-specified names.
std::vector<size_t> select_targets(const Graph &graph,
                                   const std::vector<std::string> &names,
                                   bool tests,
                                   bool runnable,
                                   const std::string &filter = {});
}  // namespace clutch
