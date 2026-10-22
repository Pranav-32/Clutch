/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Print a message supplied by a CMake-owned generated resource.
 */
#include "message.hpp"
#include <iostream>

/// The CMake File API reports this program's evaluated executable path.
int main() { std::cout << example_message << '\n'; }
