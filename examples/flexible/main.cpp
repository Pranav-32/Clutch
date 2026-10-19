/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Link a root application to a local target and a workspace member.
 */
#include "example.hpp"
#include "greeting.hpp"
#include <iostream>

static_assert(EXAMPLE_SCOPE == root_scope, "root compile defaults apply");

/// Display values from libraries built in two different manifest scopes.
int main() { std::cout << greeting() << ' ' << number() << '\n'; }
