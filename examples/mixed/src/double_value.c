/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief C implementation linked into the mixed-language application.
 */

#include "double_value.h"

/// Add the input to itself within the interface's representable-result
/// contract.
int double_value(const int value) { return value + value; }
