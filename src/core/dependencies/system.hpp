/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Consume explicit installed interfaces without package-specific rules.
 */
#pragma once
#include "interface.hpp"

namespace clutch::dependencies {
/// Libraries are normalized at declaration: absolute files or linker base
/// names.
NativeInterface inspect_system(const ForeignRequest &request,
                               const Toolchain &tools);
}  // namespace clutch::dependencies
