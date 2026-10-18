/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Quote text for JSON strings and YAML's JSON-compatible quoted scalars.
 */
#pragma once

#include <string>
#include <string_view>

namespace clutch {
/// Escape quotes, backslashes and control bytes; preserve UTF-8 text verbatim.
std::string json_string(std::string_view value);
}  // namespace clutch
