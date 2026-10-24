/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Read a release version file and resolve optional local Git identity.
 */
#pragma once

#include "core/paths.hpp"
#include <string>

namespace clutch {
/// Read canonical major.minor.patch[-prerelease] without interpreting a script.
std::string read_project_version(const fs::path &path);
/// Match release-tag, dirty, shallow and source-archive identity conventions.
/// Git is optional; failure to identify this exact source root uses +source.
std::string project_git_version(const std::string &base, const fs::path &root);
}  // namespace clutch
