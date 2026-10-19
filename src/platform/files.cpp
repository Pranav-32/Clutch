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

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace clutch {
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