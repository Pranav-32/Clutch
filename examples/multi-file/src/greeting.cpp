/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Implement the greeting shared by two independently linked targets.
 */

#include "greeting.hpp"

namespace example
{
    /// Compose the greeting without retaining the caller's borrowed input.
    std::string greet(const std::string_view name)
    {
        return "Hello, " + std::string{name} + "!";
    }
} // namespace example
