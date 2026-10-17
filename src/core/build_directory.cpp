/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Resolve profile build paths and enforce directory ownership before
 * writes or cleanup.
 */

#include "build_directory.hpp"

#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include <toml.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace clutch {
namespace {
/// Resolve the active profile path without creating or claiming the directory.
fs::path resolve_build_directory(const Options &options,
                                 const fs::path &root,
                                 const toml::table &doc) {
  const auto *const profile = options.release ? "release" : "debug";
  std::string configured;
  if (const auto node = doc["profile"][profile]["build_dir"]; node) {
    if (const auto value = node.value<std::string>(); value && !value->empty())
      configured = *value;
    else
      throw std::runtime_error("build_dir must be a nonempty path string");
  }

  if (configured.empty())
    configured = layout::default_build_directory(options.release);

  auto directory = fs::absolute(root / configured).lexically_normal();
  if (directory != directory.root_path() && directory.filename().empty())
    directory = directory.parent_path();

  return directory;
}

/// Compare path components so sibling prefixes are never treated as
/// descendants.
bool contains(const fs::path &parent, const fs::path &child) {
  const auto [left, right] =
      std::mismatch(parent.begin(), parent.end(), child.begin(), child.end());
  (void)right;
  return left == parent.end();
}
} // namespace

/// Read the profile path separately so clean/load do not require source
/// discovery.
fs::path build_directory(const Options &options) {
  const auto root = fs::canonical(options.root);
  const auto doc = toml::parse_file((root / layout::manifest).string());
  return resolve_build_directory(options, root, doc);
}

/// Enforce reserved paths, physical path checks and existing ownership.
void validate_build_directory(const fs::path &root,
                              const fs::path &directory,
                              const bool require_owner) {
  if (contains(directory, root))
    throw std::runtime_error(
        "build directory cannot be the project root or its ancestor");

  for (const auto *reserved : {"src", "include", ".git", ".idea", layout::environment})
    if (contains(root / reserved, directory))
      throw std::runtime_error("build directory cannot be inside " +
                               std::string(reserved));

  // Inspect each existing component: a junction can redirect even a nested
  // path.
  fs::path current = directory.root_path();
  for (const auto &part : directory.relative_path()) {
    current /= part;
    if (fs::is_symlink(fs::symlink_status(current)) ||
        (fs::exists(current) && fs::canonical(current) != current))
      throw std::runtime_error("refusing redirected build path: " +
                               current.string());
  }

  // The marker binds this output directory to the physical source root.
  const auto marker = directory / layout::owner_marker;
  if (fs::is_symlink(fs::symlink_status(marker)))
    throw std::runtime_error("refusing redirected build ownership marker");

  if (fs::exists(marker)) {
    std::ifstream input(marker, std::ios::binary);
    if (const std::string owner((std::istreambuf_iterator(input)), {});
      !input || owner != root.generic_string() + "\n")
      throw std::runtime_error(
          "build directory belongs to a different project: " +
          directory.string());
  } else if (fs::exists(directory) &&
             (require_owner || !fs::is_empty(directory))) {
    throw std::runtime_error("refusing unowned nonempty build directory: " +
                             directory.string());
  }
}

/// Claim a safe output directory after checking all discovered source/include
/// paths.
void prepare_build_directory(const Project &project) {
  validate_build_directory(project.root, project.build, false);

  // A custom profile directory must never swallow a source or include tree.
  for (const auto &source : project.sources)
    if (contains(project.build, source))
      throw std::runtime_error("build directory contains a project source: " +
                               source.string());

  for (const auto &[name, sources] : project.tests)
    for (const auto &source : sources)
      if (contains(project.build, source))
        throw std::runtime_error("build directory contains test source for " +
                                 name);

  for (const auto &include : project.includes)
    if (contains(project.build, include))
      throw std::runtime_error(
          "build directory contains an include directory: " + include.string());

  fs::create_directories(project.build);
  write_if_changed(project.build / layout::owner_marker,
                   project.root.generic_string() + "\n");
}
} // namespace clutch
