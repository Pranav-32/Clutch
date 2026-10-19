/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Read process environment values without Windows code-page loss.
 */
#pragma once

#include <optional>
#include <string>

namespace clutch {
/// Copy a value as UTF-8; distinguish an absent variable from an empty value.
std::optional<std::string> environment_value(const char *name);
}  // namespace clutch