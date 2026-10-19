/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Construct backend-independent compile and link commands for
 * application and test targets.
 */

#pragma once

#include "backends/backend.hpp"
#include "core/project.hpp"
#include "toolchains/toolchain.hpp"

#include <string>
#include <vector>

namespace clutch {
struct Options;

/// One translation unit; arguments includes the compiler and complete flag
/// list.
struct Unit {
  fs::path source, object;
  std::vector<std::string> arguments;
};

/// A target/language-specific prefix header, its creator and optional COFF
/// object.
struct PrecompiledHeader {
  std::string language, driver;
  fs::path header, ide_header, companion_object;
  std::vector<fs::path> headers;
  Unit creator;
};

/// One executable or library; link includes the native driver/archiver and
/// inputs.
struct Target {
  std::string name;
  fs::path output;
  std::vector<Unit> units;
  std::vector<std::string> link;
  TargetType type = TargetType::Executable;
  bool is_test = false, build_by_default = true;
  fs::path import_library;
  std::vector<size_t> dependencies, link_dependencies;
  std::vector<fs::path> includes;
  std::vector<std::string> defines;
  std::string project_name{}, local_name{}, c_standard = "11",
                                            cxx_standard = "17";
  fs::path source_root{};
  std::vector<PrecompiledHeader> precompiled_headers{};
  std::vector<std::string> foreign_dependencies{};
  std::vector<fs::path> link_inputs{}, runtime_libraries{};
  std::vector<fs::path> additional_files{};
};

/// Resolved configuration shared by native generators, saved state and IDE
/// data. Target indices identify dependencies, never implicit target roles.
struct Graph {
  Project project;
  std::vector<Target> targets;
  Toolchain toolchain;
  BuildBackend backend;
};

/// Reject duplicate artifact producers and cycles before generating or
/// executing a build.
void validate_build_graph(const Graph &graph);

/// Create compile and link commands without writing files or launching tools.
Graph make_build_graph(Project project,
                       const Toolchain &toolchain,
                       const BuildBackend &backend,
                       const Options &options);
}  // namespace clutch
