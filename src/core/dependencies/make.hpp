/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Describe explicit public interfaces for Makefiles without a metadata
 * API.
 */
#pragma once
#include "interface.hpp"

namespace clutch::dependencies {
/// Makefiles execute only during build. Artifacts/includes must be declared.
NativeInterface inspect_make(const ForeignRequest &request,
                             const Project &project,
                             const Toolchain &tools);
}  // namespace clutch::dependencies
