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
#ifdef _WIN32
#include <chrono>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace clutch {
void publish_directory(const fs::path &source, const fs::path &destination) {
  validate_generated_path(source);
  validate_generated_path(destination);
  if (!fs::is_directory(source) || fs::exists(destination))
    throw std::runtime_error("directory publication requires an existing stage "
                             "and a new destination");

#ifdef _WIN32
  // Indexers/security scanners may briefly hold newly checked-out Git files.
  // Keep publication atomic; never replace a failed rename with a partial copy.
  constexpr auto maximum_attempts = 20;
  constexpr auto retry_delay = std::chrono::milliseconds(25);
  for (auto attempt = 0; attempt < maximum_attempts; ++attempt) {
    std::error_code error;
    fs::rename(source, destination, error);
    if (!error)
      return;
    const bool transient = error.value() == ERROR_ACCESS_DENIED ||
                           error.value() == ERROR_SHARING_VIOLATION ||
                           error.value() == ERROR_LOCK_VIOLATION;
    if (!transient || attempt + 1 == maximum_attempts)
      throw fs::filesystem_error("cannot publish directory",
                                 source,
                                 destination,
                                 error);
    std::this_thread::sleep_for(retry_delay);
    validate_generated_path(source);
    validate_generated_path(destination);
    if (fs::exists(destination))
      throw std::runtime_error(
          "directory publication destination already exists");
  }
#else
  fs::rename(source, destination);
#endif
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
