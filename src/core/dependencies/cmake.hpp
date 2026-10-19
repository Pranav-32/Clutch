/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Read CMake-evaluated public compile and link usage for selected
 * targets.
 */
#pragma once
#include "interface.hpp"

namespace clutch::dependencies {
/// A small consumer target lets CMake evaluate transitive generator expressions
/// in consumer context, including imported and interface-only libraries.
NativeInterface inspect_cmake(const ForeignRequest &request,
                              const Project &project,
                              const Toolchain &tools,
                              const BuildBackend &backend,
                              const Options &options);
}  // namespace clutch::dependencies
