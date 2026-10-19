/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Check root and member library behavior through public interfaces.
 */
#include "greeting.hpp"
#include "example.hpp"
#include <string_view>

static_assert(EXAMPLE_SCOPE == root_scope, "tests inherit root defaults");

/// Return failure if either linked library loses its expected behavior.
int main() {
  return std::string_view(greeting()) == "Hello from a flexible workspace:" &&
                 number() == target_scope
             ? 0
             : 1;
}
