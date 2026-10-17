/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Call C code from C++ and link through the C++ compiler driver.
 */

#include "double_value.h"

#include <iostream>

/// Use a small sample operand whose doubled value fits in int.
int main()
{
    constexpr auto sample_value = 21;
    std::cout << "Twice " << sample_value << " is " << double_value(sample_value)
        << ".\n";

    return 0;
}
