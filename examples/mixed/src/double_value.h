/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief C-compatible interface callable from a C++ translation unit.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Double a value; the caller must ensure the result is representable as int.
int double_value(int value);

#ifdef __cplusplus
}
#endif
