/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Resolve profile build paths and enforce directory ownership before
 * writes or cleanup.
 */

#pragma once

#include "core/paths.hpp"

namespace clutch {
struct Options;
struct Project;

/// Resolve the active profile's build_dir relative to the manifest root.
fs::path build_directory(const Options &options);
/// Validate and mark an output directory owned by this project.
void prepare_build_directory(const Project &project);
/// Reject reserved/redirected paths and mismatched ownership. When
/// require_owner is true, any existing directory must already carry this
/// project's marker. Configuration may claim an unowned directory containing
/// only the public file-API query skeleton; clean/load still require ownership.
void validate_build_directory(const fs::path &root,
                              const fs::path &directory,
                              bool require_owner);
}  // namespace clutch