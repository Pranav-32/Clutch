/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Describe project-scoped, native template outputs and their refresh.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <utility>
#include <vector>

namespace clutch {
struct Project;
struct Graph;
struct Options;

/// Inputs are immutable configuration inputs; outputs live in an owned,
/// project-specific generated directory. Git identity is refreshed at build
/// time.
struct ConfiguredFile {
  fs::path input, output, source_root;
  std::string project_name, project_version;
  bool git_version = false;
  std::vector<std::pair<std::string, std::string>> variables;
};

/// Derive one collision-resistant output/include namespace per physical
/// project.
fs::path configured_directory(const fs::path &build, const fs::path &source);
/// Render outputs after claiming the build directory, then expose them to IDEs
/// and to their owning project's compiler include paths.
void prepare_configured_files(Project &project);
/// Refresh changed contents only. Serializes simultaneous native build
/// callbacks.
void refresh_configured_files(const Project &project);
/// Form a native callback, preserving argument boundaries for every generator.
std::vector<std::string> configured_files_command(const Graph &graph,
                                                  const Options &options,
                                                  const fs::path &executable);
}  // namespace clutch
