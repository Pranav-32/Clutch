/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Configure and build native projects in a debounced edit loop.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
struct Options;
// Cap CLI delays at one minute; the idle cancellation loop remains responsive.
inline constexpr auto maximum_watch_delay_ms = 60000;

/// Polling is portable and requires no platform watcher library or interpreter.
struct WatchSettings {
  // Milliseconds; debounce requires a quiet interval after the last edit.
  int interval_ms = 250;
  int debounce_ms = 200;
  bool run = false, test = false;
  std::vector<std::string> targets, arguments, paths, ignores;
};

/// Build immediately, then react to changes until interrupted. Failed cycles
/// remain watchable. Child operations are serial and are allowed to finish.
int watch_project(const Options &options,
                  const WatchSettings &settings,
                  const fs::path &executable);
}  // namespace clutch
