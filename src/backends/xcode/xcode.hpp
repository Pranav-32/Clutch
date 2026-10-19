/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Generate native macOS Xcode projects and select application or test
 * targets for xcodebuild without an intermediate build system.
 */

#pragma once

#include "core/paths.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace clutch {
struct Graph;
struct Options;

/// Write a native command-line-tool project and shared schemes for the active
/// profile. Xcode requires Apple's Clang drivers and a full macOS Xcode
/// install.
void generate_xcode(const Graph &graph,
                    const Options &options,
                    const fs::path &clutch_executable);

/// Build exactly the selected graph targets using the configured xcodebuild.
/// Test targets are standalone executables; Clutch runs them after building.
std::vector<std::string>
xcode_build_command(const Graph &graph,
                    const Options &options,
                    const std::vector<size_t> &target_indices);
}  // namespace clutch
