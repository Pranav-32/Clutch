/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief A shared C declaration callable from C++ translation units.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Multiply the example's small integer inputs.
int example_multiply(int left, int right);

#ifdef __cplusplus
}
#endif
