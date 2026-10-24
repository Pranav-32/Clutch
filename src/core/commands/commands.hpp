/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Named argument-vector commands and ordered CLI lifecycle hooks.
 */
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace clutch {
struct Options;
struct Project;

/// A root-owned command; an empty phase list makes it manual-only.
struct ProjectCommand {
  std::string name;
  std::vector<std::string> argv;
  std::string working_dir = ".";
  std::vector<std::string> phases;
};

/// Preserve a failed hook's child exit status through configuration errors.
class CommandFailure : public std::runtime_error {
  public:
  CommandFailure(const std::string &name, const int status)
      : std::runtime_error("command '" + name + "' failed (exit " +
                           std::to_string(status) + ")"),
        status_(status) {}

  int status() const noexcept { return status_; }

  private:
  int status_;
};

/// Run matching hooks serially, stopping and throwing on the first failure.
void run_command_hooks(const Project &project,
                       const Options &options,
                       const std::string &phase);
/// Run a named root command without requiring sources or saved configuration.
/// Additional CLI arguments are forwarded literally, after the stored argv.
int execute_project_command(const Options &options,
                            const std::string &name,
                            const std::vector<std::string> &arguments);
}  // namespace clutch
