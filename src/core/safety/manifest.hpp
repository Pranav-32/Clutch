/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Keep manifest parsing separate from lightweight safety policy records.
 */
#pragma once

#include "settings.hpp"
#include <toml.hpp>

namespace clutch {
/// Read a safety table; reject unknown keys, conflicts and incorrect types.
SafetySettings read_safety(const toml::table &table);
}  // namespace clutch
