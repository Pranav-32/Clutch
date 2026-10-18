/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Inspect API paths without following client-controlled redirections.
 */
#pragma once

#include "core/paths.hpp"

#include <string>
#include <vector>

namespace clutch::file_api {
/// Validate every existing component below the canonical build directory.
void validate_path(const fs::path &build, const fs::path &relative);
/// Enumerate a single directory deterministically, rejecting redirected
/// entries.
std::vector<fs::path> entries(const fs::path &build, const fs::path &relative);
/// Allow only the API query skeleton in a build directory not yet owned by
/// Clutch.
bool query_only_directory(const fs::path &build);
/// Read one regular query file with bounded memory, including concurrent
/// growth.
std::string read_query(const fs::path &path);
}  // namespace clutch::file_api
