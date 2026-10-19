/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Plan isolated prefix headers and translate IDE commands for native
 * builds.
 */
#pragma once

#include "core/build_graph.hpp"

namespace clutch::pch {
/// Add creator commands and textual prefix flags without launching a compiler.
void plan(Target &target,
          const TargetSpec &spec,
          const Project &project,
          const Toolchain &toolchain);
/// Write wrapper headers only after the build directory has been claimed.
void write_headers(const Graph &graph);
/// Substitute native binary-PCH options for the IDE's textual prefix options.
std::vector<std::string> build_arguments(const Target &target,
                                         const Unit &unit);
/// Return the prefix belonging to this source language, or nullptr.
const PrecompiledHeader *for_unit(const Target &target, const Unit &unit);
}  // namespace clutch::pch
