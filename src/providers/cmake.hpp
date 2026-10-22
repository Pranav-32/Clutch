/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Orchestrate complete CMake projects without translating their rules.
 */
#pragma once

#include "core/options.hpp"
#include <string>
#include <vector>

namespace clutch::providers {
/// Explicit workflow settings are separate from native Clutch generators.
struct CMakeRequest {
  std::string operation;
  std::string preset;
  std::string configuration;
  std::vector<std::string> arguments;
  std::vector<std::string> targets;
  std::vector<std::string> runtime_arguments;
  std::string filter;
  std::string prefix;
  std::string component;
};

/// Dispatch only for an explicit provider or a CMake-only source directory.
bool uses_cmake(const Options &options, const std::string &provider);

/// Configure the original source root or execute its CMake-owned build tree.
/// Preserve child exit codes; invalid or missing metadata throws before run.
int run_cmake(const Options &options, const CMakeRequest &request);
}  // namespace clutch::providers
