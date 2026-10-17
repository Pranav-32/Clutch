/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Serialize exact translation-unit arguments as a build-local IDE
 * compilation database.
 */

#pragma once

namespace clutch
{
    struct Graph;

    /// Write build-local compile_commands.json using the graph's exact per-source
/// arguments.
    void write_compilation_database(const Graph& graph);
} // namespace clutch
