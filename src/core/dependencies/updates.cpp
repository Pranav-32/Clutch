/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Compare a proposed package graph with the current locked revisions.
 */
#include "updates.hpp"

#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "core/workspace/inputs.hpp"
#include "git.hpp"
#include "resolve.hpp"
#include <map>
#include <stdexcept>
#include <utility>

namespace clutch::dependencies {
std::vector<Update> check_updates(const Options &options) {
  if (options.offline)
    throw std::runtime_error(
        "dependency update checks require online resolution");

  auto project = load_project(options);
  const auto lock = project.root / layout::lock_file;
  if (!fs::is_regular_file(lock)) {
    if (project.dependency_requests.empty())
      return {};

    throw std::runtime_error("dependency update checks require clutch.lock; "
                             "configure the project first");
  }

  const auto lock_signature = input_signature(lock);
  const auto pins = read_pins(project.root);
  auto proposal = options;
  proposal.update_dependencies = true;
  proposal.locked = false;
  resolve(project, proposal);
  validate_inputs(project.inputs);
  if (input_signature(lock) != lock_signature)
    throw std::runtime_error(
        "clutch.lock changed during the dependency update check; retry");

  // Compare the entire solved graph, including additions/removals introduced by
  // transitive updates. Independent per-package "latest" queries can propose
  // incompatible versions and must not be presented as one usable update.
  std::map<std::string, Update> changes;
  for (const auto &pin : pins)
    if (pin.provider == "git")
      changes[identity(pin)] = {identity(pin), pin, std::nullopt};

  for (const auto &package : project.packages)
    if (package.provider == "git") {
      auto &change = changes[identity(package)];
      change.source = identity(package);
      change.available = package;
    }

  std::vector<Update> result;
  for (auto &[source, change] : changes) {
    (void)source;
    if (!change.previous || !change.available ||
        change.previous->revision != change.available->revision)
      result.push_back(std::move(change));
  }
  return result;
}
}  // namespace clutch::dependencies
