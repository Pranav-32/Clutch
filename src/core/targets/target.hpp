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
};

/// Manifest/cache spelling and public code-model spelling have distinct roles.
std::string target_type_name(TargetType type);
std::string target_model_type(TargetType type);
TargetType parse_target_type(const std::string &name);
}  // namespace clutch
