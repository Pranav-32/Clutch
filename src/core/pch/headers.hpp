/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Read opt-in precompiled-header lists independently for C and C++.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <toml.hpp>
#include <vector>

namespace clutch::pch {
struct Headers {
  std::vector<std::string> c, cpp;
};

/// An array selects C++ headers; a table accepts separate c/cpp arrays.
Headers read(const toml::node *node, const std::string &context);
/// Resolve regular header files and retain first-occurrence include order.
std::vector<fs::path> paths(const std::vector<std::string> &headers,
                            const fs::path &root);
}  // namespace clutch::pch
