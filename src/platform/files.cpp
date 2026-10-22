/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Write generated text files only when their content changes, preserving
 * incremental build timestamps.
 */

#include "files.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace clutch {
std::string file_time_text(const fs::file_time_type timestamp) {
  // libc++ can use a signed 128-bit rep, which has no ostream overload. Work
  // in native ticks rather than narrowing or rounding to another duration.
  constexpr auto decimal_base = 10;
  auto ticks = timestamp.time_since_epoch().count();
  const bool negative = ticks < 0;
  std::string result;
  do {
    const auto remainder = ticks % decimal_base;
    const auto digit = remainder < 0 ? -remainder : remainder;
    result += static_cast<char>('0' + digit);
    ticks /= decimal_base;
  } while (ticks != 0);

  // Negate only a single digit, never the whole most-negative native value.
  if (negative)
    result += '-';

  std::reverse(result.begin(), result.end());
  return result;
}

/// Generated files must never redirect a write into source or unrelated data.
void validate_generated_path(const fs::path &path) {
  if (const auto destination = fs::absolute(path).lexically_normal();
      fs::is_symlink(fs::symlink_status(destination)) ||
      fs::weakly_canonical(destination.parent_path()) !=
          destination.parent_path())
    throw std::runtime_error("refusing redirected generated file: " +
                             path.u8string());
}

/// Preserve generated-file timestamps when bytes match; throw on write failure.
void write_if_changed(const fs::path &path, const std::string &content) {
  const auto destination = fs::absolute(path).lexically_normal();
  validate_generated_path(destination);
  // Unchanged rule/response files must not trigger unnecessary backend
  // rebuilds.
  if (std::ifstream input(destination, std::ios::binary); input) {
    if (const std::string old((std::istreambuf_iterator(input)), {});
        old == content)
      return;
  }

  // Create parents only when writing is necessary; binary mode keeps bytes
  // exact.
  fs::create_directories(destination.parent_path());
  validate_generated_path(destination);
  // Keep the standard bitmask type; unsigned casts lose ios::openmode on GNU.
  std::ofstream out(destination, std::ios::binary | std::ios::trunc);
  out << content;
  out.close();
  if (!out)
    throw std::runtime_error("cannot write " + destination.u8string());
}
}  // namespace clutch
