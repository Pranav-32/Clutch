/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Consume an ordinary CMake library through its public target interface.
 */
#include <greeting.hpp>
#include <iostream>

int main() { std::cout << greeting() << '\n'; }
