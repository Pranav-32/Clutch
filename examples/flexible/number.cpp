/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Specialize the root compile defaults for one static library.
 */
#include "example.hpp"

static_assert(EXAMPLE_SCOPE == target_scope, "target overrides root defaults");

/// Return the target-specific configuration value to the root application.
int number() { return EXAMPLE_SCOPE; }
