/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Consume a linked library's public include directory.
 */
#include "greeting.hpp"
#include <iostream>

/// Display the greeting supplied by a separate archive target.
int main() { std::cout << greeting() << '\n'; }