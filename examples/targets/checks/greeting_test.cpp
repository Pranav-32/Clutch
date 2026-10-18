/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Verify public definitions and the linked library's result.
 */
#include "greeting.hpp"

/// Return a process exit status understood by clutch test.
int main() {
  static_assert(GREETING_VERSION == 1, "public target definition missing");
  return greeting() == "Hello from a Clutch library!" ? 0 : 1;
}