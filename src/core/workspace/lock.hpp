/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Portable project resolution records, separate from host build
 * caches.
 */
#pragma once

namespace clutch {
struct Graph;
/// Check compatibility and require an existing matching lock in frozen mode.
void validate_lock_file(const Graph &graph, bool locked);
/// Atomically write changed resolution and record it as a configuration input.
void update_lock_file(Graph &graph, bool locked);
}  // namespace clutch
