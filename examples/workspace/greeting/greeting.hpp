/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Public interface exported to another workspace member.
 */
#pragma once
#include <string_view>
/// Return a message stored in the greeting library.
std::string_view greeting();
