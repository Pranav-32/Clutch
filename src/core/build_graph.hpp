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

/// One executable and its units; link includes the driver and all object
/// inputs.
struct Target {
  std::string name;
  fs::path output;
  std::vector<Unit> units;
  std::vector<std::string> link;
};

/// Resolved configuration shared by native generators, saved state and IDE
/// data. targets[0] is the application; remaining entries are independent
/// tests.
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
} // namespace clutch