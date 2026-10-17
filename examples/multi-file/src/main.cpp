/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Link a separate implementation and demonstrate runtime arguments.
 */

#include "greeting.hpp"

#include <iostream>
#include <string_view>

/// Greet the first supplied argument, or use Clutch as the default name.
int main(const int argc, char* argv[])
{
    const auto name = std::string_view{argc > 1 ? argv[1] : "Clutch"};
    std::cout << example::greet(name) << '\n';

    return 0;
}
