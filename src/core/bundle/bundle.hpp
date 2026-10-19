/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Snapshot a project's sources and resolved dependency sources.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
struct Options;

/// Resolve cached pins offline, relocate source imports and create a new ZIP.
/// Exclusions are archive-relative file/directory paths, not shell patterns.
/// Installed SDKs remain prerequisites; generated host-specific rules are
/// omitted.
fs::path bundle_project(const Options &options,
                        const fs::path &output,
                        const std::vector<std::string> &exclusions);
}  // namespace clutch
