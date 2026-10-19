/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Generate native Visual Studio C++ projects and select their MSBuild
 * targets without introducing a CMake or Ninja dependency.
 */

#pragma once

#include "core/paths.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace clutch {
struct Graph;
struct Options;

/// Write one solution and native C++ project per application/test target.
void generate_visual_studio(const Graph &graph,
                            const Options &options,
                            const fs::path &clutch_executable);

/// Construct an MSBuild invocation restricted to the requested graph targets.
std::vector<std::string>
visual_studio_build_command(const Graph &graph,
                            const Options &options,
                            const std::vector<size_t> &target_indices);
}  // namespace clutch
