/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Public interface of a local static-library target.
 */
#pragma once
#include <string_view>

/// Return a greeting owned by the library with static storage duration.
std::string_view greeting();