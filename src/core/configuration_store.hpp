/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Persist resolved build graphs so builds consume configuration outputs.
 */

#pragma once

#include "core/build_graph.hpp"

namespace clutch
{
    struct Options;

    /// Save the complete graph and the manifest signature in the profile directory.
    void save_configuration(const Graph& graph, const Options& options);

    /// Load an existing configuration; require explicit reconfiguration when stale.
/// Also reject incompatible schemas, moved roots and mismatched profile paths.
    Graph load_configuration(const Options& options);
} // namespace clutch
