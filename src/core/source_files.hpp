/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Source-discovery suffixes shared by manifest loading and tooling
 * replies.
 */
#pragma once

#include <array>

namespace clutch {
inline constexpr std::array source_extensions{".c", ".cc", ".cpp", ".cxx"};
}  // namespace clutch
