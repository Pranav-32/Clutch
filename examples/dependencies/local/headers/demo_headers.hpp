/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief A plain local header directory without a build-system manifest.
 */
#pragma once

namespace demo_headers {
// Stable sample value used to verify a separately compiled package.
inline constexpr auto expected_answer = 42;
}  // namespace demo_headers
