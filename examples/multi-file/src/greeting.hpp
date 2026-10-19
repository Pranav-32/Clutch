/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Greeting interface shared by the application and its test.
 */

#pragma once

#include <string>
#include <string_view>

namespace example {
/// Return an owning greeting; the input view is only borrowed during this call.
std::string greet(std::string_view name);
}  // namespace example
