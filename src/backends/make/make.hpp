/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Generate native GNU Make, NMake and JOM rules from Clutch's target
 * graph.
 */

#pragma once

#include "core/paths.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace clutch
{
    struct Graph;
    struct Options;

    /// Write Makefile rules and compiler response files without compiling sources.
    void generate_make(const Graph& graph,
                       const Options& options,
                       const fs::path& clutch_executable);

    /// Create a backend invocation; launch it with the profile build directory as
/// cwd. An empty selection builds the application; explicit indices select
/// graph targets.
    std::vector<std::string>
    make_build_command(const Graph& graph,
                       const Options& options,
                       const std::vector<size_t>& target_indices);
} // namespace clutch
