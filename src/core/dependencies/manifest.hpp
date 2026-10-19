/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Read strict Git dependency declarations without downloading anything.
 */
#pragma once

#include "dependency.hpp"
#include <toml.hpp>

namespace clutch::dependencies {
/// Validate selectors and aliases; local Git paths become canonical source IDs.
std::vector<Requirement> read_requirements(const toml::table &document,
                                           const fs::path &root);
}  // namespace clutch::dependencies
