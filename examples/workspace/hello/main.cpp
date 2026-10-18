/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Build an executable using another member's public interface.
 */
#include "greeting.hpp"
#include <iostream>

/// Print the message provided by the linked member library.
int main() { std::cout << greeting() << '\n'; }
