/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Remove an explicit generated artifact set with containment checks.
 */
#pragma once

#include "core/paths.hpp"
#include <vector>

namespace clutch {
/// Preflight every path and directory descendant before removing any output.
/// The caller must establish build-directory ownership first. Reject root,
/// escaping, unnormalized and redirected paths; retain all unlisted files.
void remove_generated_artifacts(const fs::path &directory,
                                const std::vector<fs::path> &artifacts);
}  // namespace clutch
