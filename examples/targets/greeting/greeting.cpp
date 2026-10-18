/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Implement the library independently of application/test entry
 * points.
 */
#include "greeting.hpp"

std::string_view greeting() { return "Hello from a Clutch library!"; }