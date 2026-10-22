/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Construct argv callbacks without serializing shell-specific policy.
 */
#include "foreign_actions.hpp"
#include "core/build_graph.hpp"
#include "core/options.hpp"

namespace clutch {
std::vector<std::string> foreign_action_command(const Graph &graph,
                                                const Options &options,
                                                const fs::path &executable,
                                                const std::string &id) {
  std::vector<std::string> command{executable.u8string(),
                                   "__foreign",
                                   "--project",
                                   graph.project.root.u8string(),
                                   "--dependency",
                                   id,
                                   "--jobs",
                                   "1"};
  if (options.release)
    command.emplace_back("--release");

  return command;
}

std::vector<std::string> foreign_runtime_command(const Graph &graph,
                                                 const Options &options,
                                                 const fs::path &executable,
                                                 const size_t target) {
  std::vector<std::string> command{executable.u8string(),
                                   "__foreign-runtime",
                                   "--project",
                                   graph.project.root.u8string(),
                                   "--target",
                                   graph.targets.at(target).name};
  if (options.release)
    command.emplace_back("--release");

  return command;
}
}  // namespace clutch
