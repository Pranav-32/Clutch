/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Test the library through its exported workspace interface.
 */
#include "greeting.hpp"

/// Signal success only when the expected library was linked.
int main() { return greeting() == "Hello from a Clutch workspace!" ? 0 : 1; }
