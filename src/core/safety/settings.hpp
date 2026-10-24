/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Portable, opt-in compile diagnostics and runtime instrumentation.
 */
#pragma once

#include <string>
#include <vector>

namespace clutch {
/// Runtime detectors are independent of optimization and debug information.
struct SafetySettings {
  std::vector<std::string> sanitizers{};
  bool lifetime = false;
  std::string strictness = "normal";
};

}  // namespace clutch
