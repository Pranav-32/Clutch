/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Select package sources independently of their native build adapters.
 */
#pragma once

#include "git.hpp"
#include <memory>

namespace clutch {
struct Options;
}

namespace clutch::dependencies {
/// Git is discovered lazily: local and installed libraries do not require it.
class Sources {
  public:
  Sources(const fs::path &root, const Options &options);
  void normalize(Requirement &requirement);
  std::vector<Candidate> candidates(const std::string &source,
                                    const std::vector<Request> &requests);
  Candidate materialize(Candidate candidate);
  bool accepts(const Candidate &candidate, const Request &request) const;
  void explore_alternatives(bool preserve_pins = true);

  private:
  fs::path root_;
  bool offline_, locked_, explore_ = false, preserve_pins_ = true;
  std::unique_ptr<GitStore> git_;
  std::vector<Package> pins_;
  std::map<std::pair<std::string, std::string>, fs::path> catalogs_;
  GitStore &git();
};
}  // namespace clutch::dependencies
