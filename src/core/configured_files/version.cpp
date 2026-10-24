/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Derive reproducible release or local development identity without
 * CMake.
 */
#include "version.hpp"
#include "core/dependencies/version.hpp"
#include "platform/capture.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace clutch {
namespace {
// Keep version input bounded independently of arbitrary template file sizes.
constexpr auto maximum_version_bytes = size_t{4096};
constexpr auto abbreviated_commit_digits = 12;

/// Strip file/command line endings while retaining the complete version token.
std::string trim(std::string text) {
  const auto space = [](const unsigned char c) {
    return std::isspace(c) != 0;
  };
  text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
  text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(),
             text.end());
  return text;
}

/// Release inputs intentionally exclude a tag prefix and build metadata.
bool canonical(const std::string &version) {
  return !version.empty() && version.front() != 'v' &&
         version.find('+') == std::string::npos &&
         dependencies::parse_version(version).has_value();
}
}  // namespace

std::string read_project_version(const fs::path &path) {
  if (fs::file_size(path) > maximum_version_bytes)
    throw std::runtime_error("project.version_file exceeds size limit");

  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read project.version_file: " +
                             path.u8string());

  std::string content(maximum_version_bytes + 1U, '\0');
  input.read(content.data(), static_cast<std::streamsize>(content.size()));
  if (input.bad() ||
      static_cast<size_t>(input.gcount()) > maximum_version_bytes)
    throw std::runtime_error("cannot read bounded project.version_file");

  content.resize(static_cast<size_t>(input.gcount()));
  auto version = trim(std::move(content));
  if (!canonical(version))
    throw std::runtime_error("project.version_file must contain a canonical "
                             "major.minor.patch[-prerelease] version");

  return version;
}

std::string project_git_version(const std::string &base, const fs::path &root) {
  if (!canonical(base))
    throw std::runtime_error(
        "git_version requires a canonical project version");

  auto fallback = base + "+source";
  const auto git = find_program("git");
  // A source archive nested inside somebody else's repository is not that
  // repository. A worktree's .git file is a valid identity boundary too.
  if (git.empty() || !fs::exists(root / ".git"))
    return fallback;

  const auto query = [&](const std::vector<std::string> &arguments) {
    std::vector<std::string> command{git, "-C", root.u8string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    auto result = capture(command, root);
    result.text = trim(std::move(result.text));
    return result;
  };
  const auto git_root = query({"rev-parse", "--show-toplevel"});
  if (std::error_code error;
      git_root.status ||
      !fs::equivalent(root, fs::u8path(git_root.text), error) || error)
    return fallback;

  const auto commit =
      query({"rev-parse",
             "--short=" + std::to_string(abbreviated_commit_digits),
             "HEAD"});
  const auto changes =
      query({"status", "--porcelain", "--untracked-files=normal"});
  if (commit.status || changes.status || commit.text.empty() ||
      !std::all_of(commit.text.begin(),
                   commit.text.end(),
                   [](const unsigned char c) {
                     return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                   }))
    return fallback;

  const auto tag = query({"tag", "--points-at", "HEAD", "--list", "v" + base});
  if (!tag.status && tag.text == "v" + base && changes.text.empty())
    return base;

  auto identity = base +
                  (base.find('-') == std::string::npos ? "-dev+g" : ".dev+g") +
                  commit.text;
  if (!changes.text.empty())
    identity += ".dirty";

  if (const auto shallow = query({"rev-parse", "--is-shallow-repository"});
      !shallow.status && shallow.text == "true")
    identity += ".shallow";

  return identity;
}
}  // namespace clutch
