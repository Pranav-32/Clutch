/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Use a member override and a header inherited from the workspace root.
 */
#include "greeting.hpp"
#include "example.hpp"

static_assert(EXAMPLE_SCOPE == member_scope, "member overrides root defaults");

const char *greeting() { return "Hello from a flexible workspace:"; }
