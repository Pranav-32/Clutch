/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Publish query/reply snapshots independently of editor implementations.
 */
#pragma once

#include "core/paths.hpp"

#include <memory>
#include <string>

namespace clutch {
struct Graph;
struct Options;
}  // namespace clutch

namespace clutch::file_api {
/// Machine-readable discovery; neither operation configures or writes a
/// project.
std::string capabilities_json();
std::string locate_json(const Options &options);

/// One writer per profile. Objects become visible through an immutable index
/// only after generation succeeds. Failed attempts publish an error index.
class Session {
  public:
  explicit Session(const Options &options);
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  /// Snapshot requests and lock an already validated/owned build directory.
  void begin(const fs::path &build);
  /// Publish all requested objects after native outputs and saved state
  /// succeed.
  void publish(const Graph &graph, const fs::path &executable);
  /// Record a failed configuration; throw if an API write itself fails.
  void fail(const std::string &message);

  private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace clutch::file_api
