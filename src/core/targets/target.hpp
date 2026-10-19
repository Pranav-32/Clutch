/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Generator-independent target declarations and usage requirements.
 */
#pragma once

#include "core/paths.hpp"

#include <string>
#include <vector>

namespace clutch {
enum class TargetType {
  Executable,
  StaticLibrary,
  SharedLibrary
};

/// Evaluated public usage of a foreign or installed library. Native build
/// systems retain ownership of its source graph, generated headers and ABI.
struct DependencyUsage {
  std::string id;
  std::vector<fs::path> includes, link_inputs, runtime_libraries;
  std::vector<std::string> defines, c_options, cpp_options, link_options;
  std::string c_standard, cpp_standard;
  bool needs_cpp = false;
};

/// Names are project-local. Public requirements propagate through public links;
/// a consumer also receives public requirements of its direct private links.
struct TargetSpec {
  std::string name;
  std::vector<fs::path> sources;
  TargetType type = TargetType::Executable;
  std::vector<fs::path> includes, public_includes;
  std::vector<std::string> defines, public_defines, links, public_links;
  bool is_test = false;
  bool build_by_default = true;
  // Populated when composing workspaces; empty values inherit standalone
  // policy.
  std::string project_name{}, local_name{}, c_standard{}, cxx_standard{};
  fs::path source_root{};
  bool external =
      false;  ///< Imported libraries retain their own compile policy.
  std::vector<fs::path> pch_c, pch_cpp;
  std::vector<DependencyUsage> dependency_usage;
  std::vector<fs::path>
      additional_files{};  ///< IDE files, never translation units.
};

/// Manifest/cache spelling and public code-model spelling have distinct roles.
std::string target_type_name(TargetType type);
std::string target_model_type(TargetType type);
TargetType parse_target_type(const std::string &name);
}  // namespace clutch
