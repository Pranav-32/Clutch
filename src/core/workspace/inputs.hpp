/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Fingerprint every manifest and lock input used by
 * configuration.
 */
#pragma once
#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
/// A byte signature detects edits independently of filesystem timestamps.
struct ConfigurationInput {
  fs::path path;
  std::string signature;
};

/// FNV-1a is a change detector, not an integrity or authentication guarantee.
std::string input_signature(const fs::path &path);
/// Fail when configuration no longer describes the inputs it actually parsed.
void validate_inputs(const std::vector<ConfigurationInput> &inputs);
}  // namespace clutch
