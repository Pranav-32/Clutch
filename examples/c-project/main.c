/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief A C-only application compiled with the manifest's C11 standard.
 */

#include <stdio.h>

/// Print a greeting without requiring a C++ translation unit.
int main(void)
{
    puts("Hello from Clutch and C!");

    return 0;
}
