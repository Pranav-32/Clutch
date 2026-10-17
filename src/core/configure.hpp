/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Coordinate manifest loading, tool resolution, graph construction and
 * output generation.
 */

#pragma once

#include "core/build_graph.hpp"

namespace clutch
{
    struct Options;

    /// Resolve a project and generate its backend files, IDE database and tool
/// caches.
/// Throws on invalid inputs or output failures; no compilation is performed.
    Graph configure(const Options& options, const fs::path& executable);
} // namespace clutch
