/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Expose strict configured-file manifest parsing.
 */
#pragma once
#include <toml.hpp>

namespace clutch {
struct Project;
/// Parse strict template declarations without creating files or directories.
void read_configured_files(Project &project, const toml::table &document);
}  // namespace clutch
