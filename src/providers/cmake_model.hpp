/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Read bounded, referenced CMake File API facts for project workflows.
 */
#pragma once

#include "core/paths.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clutch::providers {
/// An evaluated target retains its CMake type and reported output paths.
struct CMakeTarget {
  std::string name;
  std::string type;
  std::vector<fs::path> artifacts;
  fs::path
      executable;  ///< Artifact matching CMake's nameOnDisk, excluding PDBs.
};

/// A configuration snapshot; no foreign commands are converted to native rules.
struct CMakeModel {
  fs::path source;
  fs::path build;
  fs::path index;
  std::string cmake;
  std::string ctest;
  std::string cpack;
  std::string configuration;
  std::string generator;
  std::vector<CMakeTarget> targets;
};

/// Limit metadata allocations and reject missing, redirected or malformed
/// files.
nlohmann::json read_cmake_json(const fs::path &path);

/// Follow only references published by the current index for this client.
/// An empty requested configuration accepts a single-configuration build;
/// otherwise the name must match exactly.
CMakeModel read_cmake_model(const fs::path &source,
                            const fs::path &build,
                            const std::string &configuration);
}  // namespace clutch::providers
