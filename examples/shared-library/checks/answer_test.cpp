/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Link a separate consumer against the same shared implementation.
 */
#include <answer.hpp>

int main() { return shared_answer() == 42 ? 0 : 1; }
