/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Describe compatible dependency updates without publishing new pins.
 */
#pragma once

#include "dependency.hpp"
#include <optional>

namespace clutch {
struct Options;
}

namespace clutch::dependencies {
/// Added/removed transitive packages have only one side of the comparison.
struct Update {
  std::string source;
  std::optional<Package> previous, available;
};

/// Resolve eligible updates as one compatible graph using the normal solver.
/// May populate the source cache, but preserves locks and build configuration.
std::vector<Update> check_updates(const Options &options);
}  // namespace clutch::dependencies
