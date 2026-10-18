/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Parse explicit target and executable-test declarations.
 */
#pragma once

#include <toml.hpp>

namespace clutch {
struct Project;
/// Resolve target-local paths, reject ambiguous declarations and duplicate
/// names.
void load_manifest_targets(const toml::table &document, Project &project);
}  // namespace clutch
