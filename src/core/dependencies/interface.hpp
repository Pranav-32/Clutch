/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Share evaluated usage and optional build actions across package
 * adapters.
 */
#pragma once
#include "core/targets/target.hpp"
#include "core/workspace/inputs.hpp"
#include "dependency.hpp"

namespace clutch {
struct Project;
struct Toolchain;
struct BuildBackend;
struct Options;
}  // namespace clutch

namespace clutch::dependencies {
/// Installed interfaces have no build action; foreign sources keep native ones.
struct NativeInterface {
  DependencyUsage usage;
  ForeignBuild build;
  std::vector<ConfigurationInput> inputs;
  std::string version;
};
}  // namespace clutch::dependencies
