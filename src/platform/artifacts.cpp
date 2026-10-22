/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Validate generated cleanup paths before deleting selected products.
 */
#include "artifacts.hpp"
#include "files.hpp"
#include <stdexcept>

namespace clutch {
void remove_generated_artifacts(const fs::path &directory,
                                const std::vector<fs::path> &artifacts) {
  if (!directory.is_absolute() || directory != directory.lexically_normal() ||
      !fs::is_directory(directory) || fs::canonical(directory) != directory)
    throw std::runtime_error("invalid or redirected artifact directory");

  // Complete the entire validation pass first: a bad later entry must not
  // remove an earlier valid product. This is not a concurrent-process sandbox.
  for (const auto &path : artifacts) {
    const auto relative = path.lexically_relative(directory);
    if (!path.is_absolute() || path != path.lexically_normal() ||
        relative.empty() || relative == "." || relative.is_absolute() ||
        *relative.begin() == "..")
      throw std::runtime_error(
          "refusing to clean an artifact outside the build directory");

    validate_generated_path(path);
    if (fs::exists(path) && fs::canonical(path) != path)
      throw std::runtime_error("refusing to clean a redirected artifact");

    if (fs::is_directory(path))
      for (const auto &entry : fs::recursive_directory_iterator(path)) {
        validate_generated_path(entry.path());
        if (fs::canonical(entry.path()) != entry.path())
          throw std::runtime_error("refusing to clean a redirected artifact");
      }
  }

  for (const auto &path : artifacts)
    fs::remove_all(path);
}
}  // namespace clutch
