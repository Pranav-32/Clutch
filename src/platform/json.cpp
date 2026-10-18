/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Encode JSON string values without applying shell escaping.
 */
#include "json.hpp"

#include "core/encoding_constants.hpp"

namespace clutch {
std::string json_string(const std::string_view value) {
  std::string result = "\"";
  for (const unsigned char character : value) {
    if (character == '"' || character == '\\') {
      result += '\\';
      result += static_cast<char>(character);
    } else if (character < encoding::first_printable_ascii) {
      constexpr auto hex_digits = "0123456789abcdef";
      const auto byte = static_cast<unsigned int>(character);
      result += "\\u00";
      result += hex_digits[byte >> encoding::bits_per_nibble];
      result += hex_digits[byte & encoding::low_nibble_mask];
    } else {
      result += static_cast<char>(character);
    }
  }

  return result + '"';
}
}  // namespace clutch
