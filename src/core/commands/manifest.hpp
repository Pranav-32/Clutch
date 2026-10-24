/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Validate named command declarations without loading project sources.
 */
#pragma once

#include "commands.hpp"
#include <toml.hpp>

namespace clutch {
/// Read optional [[command]] tables, validating names, argv and hook phases.
/// The same parser validates persisted commands before any child is launched.
std::vector<ProjectCommand> read_project_commands(const toml::table &document);
}  // namespace clutch
