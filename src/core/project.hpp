/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Validate clutch.toml and resolve project sources, standards, profiles
 * and tests.
 */

#pragma once

#include "core/paths.hpp"
#include "core/targets/target.hpp"
#include "core/workspace/inputs.hpp"

#include <string>
#include <vector>

namespace clutch {
struct Options;

/// A real project manifest; virtual workspace roots have no project record.
struct WorkspaceProject {
  std::string name, version;
  fs::path root;
};

/// Validated manifest settings with canonical inputs and an absolute output
/// root. Defaults define C11/C++17, Ninja and extra warnings before profile
/// overrides.
struct Project {
  // Physical manifest root, active profile directory and application output.
  fs::path root, build, output;
  std::string name, cxx_standard = "17", c_standard = "11";
  std::string language = "auto", generator = "ninja";
  std::string optimize, warnings = "extra";
  // Inputs are resolved before generation; headers are found through includes.
  std::vector<fs::path> sources, includes;
  // Definitions omit driver prefixes; graph construction supplies -D or /D.
  std::vector<std::string> defines;

  /// An independently linked executable that inherits the project's compile
  /// policy.
  using Test = TargetSpec;
  std::vector<TargetSpec> targets;
  std::vector<Test> tests;
  bool is_workspace = false;
  std::string version;
  std::vector<WorkspaceProject> projects;
  std::vector<ConfigurationInput> inputs;
};

/// Parse and validate clutch.toml, discovering source files and normalizing
/// project settings.
Project load_project(const Options &options);
}  // namespace clutch
