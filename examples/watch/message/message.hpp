/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief A header edit is an ordinary watched build input.
 */
#pragma once

/// Prefix shared by the example application and test.
inline constexpr auto message_prefix = "Resource: ";

/// Return the default greeting when the resource file is empty.
const char *default_message() noexcept;
