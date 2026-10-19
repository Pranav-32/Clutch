/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Compose compile defaults and local overrides before native generation.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
/// Search local directories before defaults, retaining each canonical path
/// once.
std::vector<fs::path> merge_includes(const std::vector<fs::path> &defaults,
                                     const std::vector<fs::path> &local);

/// Replace defaults by macro identifier, emitting only one definition per name.
/// Function-like definitions use the identifier before the opening parenthesis.
std::vector<std::string> merge_defines(const std::vector<std::string> &defaults,
                                       const std::vector<std::string> &local);
}  // namespace clutch
