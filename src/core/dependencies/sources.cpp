/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Preserve live local sources while Git owns immutable checkout pins.
 */
#include "sources.hpp"
#include "core/options.hpp"
#include "vcpkg.hpp"
#include "version.hpp"
#include <utility>

namespace clutch::dependencies {
Sources::Sources(const fs::path &root, const Options &options)
    : root_(root),
      offline_(options.offline),
      locked_(options.locked),
      pins_(read_pins(root)) {}

void Sources::normalize(Requirement &requirement) {
  if (requirement.provider == "vcpkg") {
    const auto repository = catalog_repository(requirement);
    const auto key = std::make_pair(repository, requirement.baseline);
    if (!catalogs_.count(key)) {
      const auto catalog = git().materialize({repository,
                                              requirement.baseline,
                                              "catalog",
                                              requirement.baseline,
                                              {}});
      catalogs_.emplace(key, catalog.source);
    }
    resolve_catalog(requirement, catalogs_.at(key));
  }

  if (requirement.provider == "path") {
    const auto path = fs::u8path(requirement.path);
    const auto relative = path.lexically_relative(root_);
    requirement.locator =
        (relative.empty() ? path : relative).generic_u8string();
  }
}

std::vector<Candidate>
Sources::candidates(const std::string &source,
                    const std::vector<Request> &requests) {
  const auto &requirement = requests.front().requirement;
  if (requirement.provider != "git")
    return {{source,
             "live",
             "",
             requirement.provider,
             requirement.provider == "path" ? fs::u8path(requirement.path)
                                            : fs::path{}}};

  return git().candidates(source, requests);
}

GitStore &Sources::git() {
  if (!git_) {
    git_ = std::make_unique<GitStore>(root_, offline_, locked_, pins_);
    if (explore_)
      git_->explore_alternatives(preserve_pins_);
  }

  return *git_;
}

Candidate Sources::materialize(Candidate candidate) {
  return candidate.reference == "path" || candidate.reference == "system" ||
                 candidate.reference == "vcpkg"
             ? candidate
             : git_->materialize(std::move(candidate));
}

bool Sources::accepts(const Candidate &candidate,
                      const Request &request) const {
  const auto &requirement = request.requirement;
  if (requirement.provider == "git")
    return git_->accepts(candidate, request);

  // Installed package versions use their provider's comparison rules. A local
  // Clutch manifest can participate in the same SemVer constraints as Git.
  return requirement.provider != "path" || candidate.version.empty() ||
         satisfies(candidate.version, requirement.version);
}

void Sources::explore_alternatives(const bool preserve_pins) {
  explore_ = true;
  preserve_pins_ = preserve_pins;
  if (git_)
    git_->explore_alternatives(preserve_pins);
}
}  // namespace clutch::dependencies
