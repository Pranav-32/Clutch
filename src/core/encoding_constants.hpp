/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Name hexadecimal encoding widths and ASCII control boundaries.
 */

#pragma once

namespace clutch::encoding
{
    // A hexadecimal digit represents a nibble; two digits encode an 8-bit byte.
    inline constexpr auto bits_per_nibble = 4U;
    inline constexpr auto low_nibble_mask = 0x0FU;
    inline constexpr auto hex_digits_per_byte = 2U;

    // Bytes below space and the DEL byte are ASCII control characters.
    inline constexpr auto first_printable_ascii = 0x20U;
    inline constexpr auto ascii_delete = 0x7FU;
} // namespace clutch::encoding
