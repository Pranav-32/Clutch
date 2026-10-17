/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Represent user-supplied command options without coupling them to a
 * concrete build generator.
 */

#pragma once

#include "core/paths.hpp"

#include <string>

namespace clutch
{
    /// Invocation overrides; empty compiler/generator strings defer to resolution.
    struct Options
    {
        // Project directory supplied positionally for configuration or by --project.
        fs::path root = fs::current_path();

        // Configuration-only driver overrides; unused languages need no compiler.
        std::string cc, cxx;

        // Executable location override; selected independently from the generator.
        std::string build_tool_executable;
        // Native build-file format; an empty override defers to the manifest.
        std::string generator;

        // Selects profile.release for both configuration and saved-graph execution.
        bool release = false;
        int jobs = 0; ///< Zero leaves parallelism at the selected backend's default.
    };
} // namespace clutch
