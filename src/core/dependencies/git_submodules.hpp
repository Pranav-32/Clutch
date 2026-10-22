/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Verify recursive gitlink worktrees through the source store's Git
 * runner.
 */
#pragma once

#include "core/paths.hpp"
#include <functional>
#include <string>
#include <vector>

namespace clutch::dependencies {
using GitOperation =
    std::function<std::string(const fs::path &,
                              const std::vector<std::string> &)>;
/// Initialize missing gitlinks at recorded commits and verify nested worktrees.
/// Offline checks never start a clone/fetch; modified worktrees are preserved.
void prepare_git_submodules(const fs::path &source,
                            bool offline,
                            const GitOperation &git);
}  // namespace clutch::dependencies
