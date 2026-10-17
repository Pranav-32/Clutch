/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Independent executable test of the public greeting contract.
 */

#include "greeting.hpp"

#include <iostream>

/// Report failures through the process exit status consumed by clutch test.
int main()
{
    if (const auto message = example::greet("Clutch");
        message != "Hello, Clutch!")
    {
        std::cerr << "Unexpected greeting: " << message << '\n';

        return 1;
    }

    if (const auto message = example::greet(""); message != "Hello, !")
    {
        std::cerr << "Unexpected greeting for an empty name: " << message << '\n';

        return 1;
    }

    std::cout << "Greeting tests passed.\n";

    return 0;
}
