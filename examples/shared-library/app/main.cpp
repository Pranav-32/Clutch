/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Consume the shared target's public header and runtime library.
 */
#include <answer.hpp>
#include <iostream>

int main() { std::cout << "Shared answer: " << shared_answer() << '\n'; }
