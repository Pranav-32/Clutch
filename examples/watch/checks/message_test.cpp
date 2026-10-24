/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief A small real test repeated by clutch watch --test.
 */
#include <cstring>
#include <iostream>
#include <message.hpp>

int main() {
  if (std::strlen(default_message()) == 0)
    return 1;

  std::cout << "Message test passed\n";
}
