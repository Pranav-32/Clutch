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
/// Preserve generated-file timestamps when bytes match; throw on write failure.
void write_if_changed(const fs::path &path, const std::string &content) {
  // Unchanged rule/response files must not trigger unnecessary backend
  // rebuilds.
  if (std::ifstream input(path, std::ios::binary); input) {
    if (const std::string old((std::istreambuf_iterator(input)), {});
      old == content)
      return;
  }

  // Create parents only when writing is necessary; binary mode keeps bytes
  // exact.
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
  if (!out)
    throw std::runtime_error("cannot write " + path.string());
}
} // namespace clutch