/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Compare semantic versions and evaluate explicit version requirements.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clutch::dependencies {
struct Version {
  std::uint64_t major = 0, minor = 0, patch = 0;
  std::vector<std::string> prerelease;
};

/// Parse canonical major.minor.patch[-prerelease][+metadata]; tags may have v.
std::optional<Version> parse_version(std::string_view text);
/// Compare precedence, ignoring build metadata as required by SemVer.
int compare(const Version &left, const Version &right);
/// Support exact versions, ^/~ ranges and whitespace/comma-separated
/// comparisons. Invalid expressions throw instead of broadening a dependency
/// requirement.
bool satisfies(const std::string &version, const std::string &requirement);
}  // namespace clutch::dependencies
