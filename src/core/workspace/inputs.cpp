/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Share input change detection between configuration and build
 * loading.
 */
#include "inputs.hpp"
#include "core/encoding_constants.hpp"
#include "core/hash_constants.hpp"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace clutch {
std::string input_signature(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read configuration input: " +
                             path.u8string());

  auto hash = hashing::fnv1a64_offset_basis;
  for (auto byte = std::istreambuf_iterator(input); byte != decltype(byte){};
       ++byte) {
    hash ^= static_cast<unsigned char>(*byte);
    hash *= hashing::fnv1a64_prime;
  }

  if (input.bad())
    throw std::runtime_error("cannot read complete configuration input: " +
                             path.u8string());

  constexpr auto hex_digits =
      static_cast<int>(sizeof(std::uint64_t) * encoding::hex_digits_per_byte);
  std::ostringstream text;
  text << std::hex << std::setw(hex_digits) << std::setfill('0') << hash;
  return text.str();
}

void validate_inputs(const std::vector<ConfigurationInput> &inputs) {
  for (const auto &[path, signature] : inputs)
    if (input_signature(path) != signature)
      throw std::runtime_error(
          "configuration input changed; configure again: " + path.u8string());
}
}  // namespace clutch
