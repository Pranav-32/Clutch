/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Name the standard FNV-1a initial states and multiplication constants.
 */

#pragma once

#include <cstdint>

namespace clutch::hashing
{
    // Standard FNV-1a offset bases initialize the 32-bit and 64-bit hash states.
    // These non-cryptographic hashes provide stable IDs and change detection only.
    // Reference:
    // https://www.ietf.org/archive/id/draft-eastlake-fnv-25.html#section-5
    inline constexpr std::uint32_t fnv1a32_offset_basis = UINT32_C(2166136261);
    inline constexpr std::uint64_t fnv1a64_offset_basis =
        UINT64_C(14695981039346656037);

    // After XORing each input byte, multiply by the corresponding FNV prime.
    // Unsigned overflow intentionally implements reduction modulo 2^32 or 2^64.
    inline constexpr std::uint32_t fnv1a32_prime = UINT32_C(16777619);
    inline constexpr std::uint64_t fnv1a64_prime = UINT64_C(1099511628211);
} // namespace clutch::hashing
