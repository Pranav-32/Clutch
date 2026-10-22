/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Resolve Git refs and publish verified project-local source checkouts.
 */
#pragma once

#include "dependency.hpp"
#include <map>

namespace clutch::dependencies {
/// A single writer protects cache publication, not compilation. Sources are
/// immutable after publication and can be read by concurrent native builds.
class GitStore {
  public:
  GitStore(const fs::path &root,
           bool offline,
           bool locked,
           std::vector<Package> pins);
  ~GitStore();
  GitStore(const GitStore &) = delete;
  GitStore &operator=(const GitStore &) = delete;

  /// Prefer a compatible locked revision; query each remote at most once.
  std::vector<Candidate> candidates(const std::string &git,
                                    const std::vector<Request> &requests);
  /// Resolve a candidate into an unchanged checkout, fetching only if needed.
  Candidate materialize(Candidate candidate);
  /// Evaluate exact selectors as well as semantic-version requirements.
  bool accepts(const Candidate &candidate, const Request &request) const;
  /// Retry an unsatisfied unlocked solve with alternatives to compatible pins.
  void explore_alternatives(bool preserve_pins = true);

  private:
  fs::path root_, cache_, writer_;
  bool offline_, locked_;
  bool explore_ = false;
  bool preserve_pins_ = true;
  std::string executable_;
  std::vector<Package> pins_;
  std::map<std::string, std::map<std::string, std::string>> remotes_;
  std::string git(const fs::path &directory,
                  const std::vector<std::string> &arguments) const;
  /// Verify source identity and cleanliness before marking a checkout complete.
  void validate_checkout(Candidate &candidate, const fs::path &directory) const;
};

/// Read dependency pins before graph resolution; legacy local-only locks have
/// no pins. Never reinterpret unknown schemas or malformed package records.
std::vector<Package> read_pins(const fs::path &root);
}  // namespace clutch::dependencies
