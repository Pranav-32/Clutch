/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Generate Ninja rules from the resolved graph, including response files
 * and regeneration.
 */

#pragma once

#include "core/paths.hpp"

#include <string>

namespace clutch
{
    struct Graph;
    struct Options;

    /// Find Ninja bundled with CMake, Visual Studio or CLion on Windows after
/// explicit selections, saved selections and PATH discovery have been tried.
    std::string discover_ninja();

    /// Serialize the resolved build graph as Ninja rules and regeneration commands.
    void generate_ninja(const Graph& graph,
                        const Options& options,
                        const fs::path& executable);
} // namespace clutch
