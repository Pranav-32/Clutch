/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Export configured compile/link dependencies for optional Graphviz
 * inspection.
 */

#pragma once

namespace clutch
{
    struct Graph;

    /// Write a deterministic build-graph.dot in the profile directory without
/// invoking Graphviz.
    void write_graph_visualization(const Graph& graph);
} // namespace clutch
