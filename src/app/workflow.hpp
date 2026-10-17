/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Create project skeletons and clean only owned profile build
 * directories.
 */

#pragma once

#include "core/paths.hpp"

#include <string>

namespace clutch
{
    struct Options;

    /// Create a new manifest and optional entry source without replacing existing
/// project files.
    void create_project(const fs::path& directory,
                        const std::string& requested_name,
                        bool c,
                        bool require_new);
    /// Remove only the active profile's owned build directory after checking for
/// redirected entries.
    void clean_project(const Options& options);
} // namespace clutch
