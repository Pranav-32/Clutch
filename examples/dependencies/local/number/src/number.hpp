/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Public interface exported by the native static-library package.
 */
#pragma once

namespace demo_number {
/// Return a stable value so consumers can check that linking succeeded.
int answer();
}  // namespace demo_number
