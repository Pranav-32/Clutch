/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Enforce query-only bootstrapping, path containment and query read
 * limits.
 */
#include "paths.hpp"

#include "protocol.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace clutch::file_api {
void validate_path(const fs::path &build, const fs::path &relative) {
  if (relative.is_absolute())
    throw std::runtime_error("file API path must be build-relative");

  auto path = build;
  for (const auto &part : relative) {
    if (part == "..")
      throw std::runtime_error("file API path escapes the build directory");

    path /= part;
    std::error_code error;
    const auto state = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
      continue;

    if (error)
      throw fs::filesystem_error("cannot inspect file API path", path, error);

    if (fs::is_symlink(state) ||
        (fs::exists(state) && fs::canonical(path) != path))
      throw std::runtime_error("refusing redirected file API path: " +
                               path.string());
  }
}

std::vector<fs::path> entries(const fs::path &build, const fs::path &relative) {
  validate_path(build, relative);
  std::vector<fs::path> result;
  if (!fs::exists(build / relative))
    return result;

  for (const auto &entry : fs::directory_iterator(build / relative)) {
    if (result.size() == maximum_entries)
      throw std::runtime_error("too many entries in file API directory");

    const auto path = relative / entry.path().filename();
    validate_path(build, path);
    result.push_back(path);
  }

  std::sort(result.begin(), result.end());
  return result;
}

bool query_only_directory(const fs::path &build) {
  const auto query = fs::path(api_directory) / "query";
  validate_path(build, query);
  if (!fs::is_directory(build / query))
    return false;

  // Only one directory is allowed at each ancestor level. Even harmless-looking
  // unrelated files must not let configure claim and later clean a user's tree.
  auto parent = fs::path{};
  for (const auto &part : query) {
    const auto children = entries(build, parent);
    if (children.size() != 1 || children.front().filename() != part)
      return false;

    parent /= part;
  }

  for (const auto &path : entries(build, query)) {
    if (fs::is_regular_file(build / path))
      continue;

    if (!fs::is_directory(build / path) ||
        path.filename().string().find("client-") != 0 ||
        path.filename().string() == "client-")
      return false;

    for (const auto &child : entries(build, path))
      if (!fs::is_regular_file(build / child))
        return false;
  }

  return true;
}

std::string read_query(const fs::path &path) {
  if (!fs::is_regular_file(path))
    throw std::runtime_error("query must be a regular file");

  if (fs::file_size(path) > maximum_query_bytes)
    throw std::runtime_error("query exceeds one MiB");

  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read query");

  std::string text(maximum_query_bytes + 1U, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<size_t>(input.gcount()));
  if (input.bad() || text.size() > maximum_query_bytes)
    throw std::runtime_error("query read failed or exceeded one MiB");

  return text;
}
}  // namespace clutch::file_api
