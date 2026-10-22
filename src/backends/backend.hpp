/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Register build generators and resolve executable locations
 * independently from compiler toolchains.
 */

#pragma once

#include "core/paths.hpp"

#include <string>
#include <vector>

namespace clutch {
struct Options;
struct Project;
struct Graph;

/// Selected native build format and the resolved executor used to build it.
struct BuildBackend {
  std::string name;
  std::string executable;
};

/// Registry metadata; host support does not imply the executor is installed.
struct GeneratorInfo {
  std::string name;
  std::string description;
  bool supported_on_host;
};

/// Normalize documented generator aliases without selecting an executable.
std::string normalize_backend_name(const std::string &name);
/// List implemented generators and their platform availability without a
/// project.
std::vector<GeneratorInfo> supported_generators();

/// Resolve the selected generator executable using overrides, environment,
/// cache and discovery.
BuildBackend resolve_backend(const Options &options, const Project &project);
/// Persist generator name and executable in the selected build directory.
void save_backend(const BuildBackend &backend, const Project &project);
/// Clean selected artifacts when a backend cannot force compilation directly.
/// Other backends combine their cleaning/forced-build step with the build
/// action.
int clean_backend_artifacts(const Graph &graph,
                            const std::vector<size_t> &target_indices);
/// Dispatch build-file generation through the selected generator driver.
void generate_backend_files(const Graph &graph,
                            const Options &options,
                            const fs::path &executable);

/// Form the selected generator's native build invocation for graph target
/// indices.
std::vector<std::string>
backend_build_command(const Graph &graph,
                      const Options &options,
                      const std::vector<size_t> &target_indices);
}  // namespace clutch
