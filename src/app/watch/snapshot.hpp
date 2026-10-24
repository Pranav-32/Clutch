/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Snapshot owned inputs while excluding outputs and immutable caches.
 */
#pragma once

#include "core/paths.hpp"
#include <map>
#include <set>
#include <string>

namespace clutch {
struct Graph;
struct Options;
struct WatchSettings;

/// Content signatures detect edits even when file size and timestamp repeat.
struct WatchSnapshot {
  std::map<fs::path, std::string> files;
  std::map<fs::path, fs::file_time_type> timestamps;
  std::set<fs::path> configuration;

  /// Swap buffers without allocating map sentinels on platforms with throwing
  /// container move constructors. The polling loop reuses its two buffers.
  void swap(WatchSnapshot &other) noexcept {
    files.swap(other.files);
    timestamps.swap(other.timestamps);
    configuration.swap(other.configuration);
  }
};

/// Include current workspace/local roots even after a failed configuration.
/// Missing/unreadable files are stable states; directory links are not
/// followed.
void snapshot_watch_inputs(const Options &options,
                           const WatchSettings &settings,
                           const Graph *graph,
                           WatchSnapshot &result);
/// Membership changes and edits to configuration inputs require regeneration.
bool watch_needs_configuration(const WatchSnapshot &before,
                               const WatchSnapshot &after);
/// Timestamp-based executors need a clean cycle for non-advancing file edits.
bool watch_needs_clean(const WatchSnapshot &before, const WatchSnapshot &after);
}  // namespace clutch
