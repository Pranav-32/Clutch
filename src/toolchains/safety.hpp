/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Translate and probe safety requests using the selected native drivers.
 */
#pragma once

#include <string>
#include <vector>

namespace clutch {
struct Project;
struct Toolchain;
struct Options;

/// Driver flags shared by graph construction, PCH and CMake source adapters.
std::vector<std::string>
safety_compile_flags(const Project &project, const Toolchain &tools, bool c);
/// Only executable/shared-library drivers receive sanitizer runtime flags.
std::vector<std::string> safety_link_flags(const Project &project,
                                           const Toolchain &tools);
/// Compile/link tiny programs, never execute them, and reject unsupported
/// policy.
void verify_safety(Project &project,
                   const Toolchain &tools,
                   const Options &options);
}  // namespace clutch
