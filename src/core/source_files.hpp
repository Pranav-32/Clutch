/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Source-discovery suffixes shared by manifest loading and tooling
 * replies.
 */
#pragma once

#include "core/paths.hpp"
#include <array>
#include <string>
#include <vector>

namespace clutch {
inline constexpr std::array source_extensions{".c", ".cc", ".cpp", ".cxx"};
inline constexpr std::array header_extensions{".h", ".hh", ".hpp", ".hxx"};

/// Compile units and noncompiled project files have separate generator roles.
struct SourceSelection {
  std::vector<fs::path> sources, additional_files;
};

/// Expand configure-time recursive filename patterns into canonical, sorted,
/// unique paths. Generated state and redirected directories are never
/// traversed.
SourceSelection expand_sources(const fs::path &root,
                               const std::vector<std::string> &patterns,
                               const fs::path &build,
                               bool require_matches = true);
}  // namespace clutch
