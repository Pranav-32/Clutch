/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Expand recursive source selections without compiling header files.
 */
#include "source_files.hpp"

#include "core/project_layout.hpp"
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clutch {
namespace {
// Bound manifest expansion and filesystem work independently of compiler jobs.
constexpr size_t maximum_pattern_variants = 256;
constexpr size_t maximum_scanned_files = 100000;

/// Match one filename with linear-space '*' and '?' wildcard backtracking.
bool matches(const std::string_view pattern, const std::string_view name) {
  size_t p = 0, n = 0, star = pattern.npos, retry = 0;
  while (n < name.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
      ++p;
      ++n;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      retry = n;
    } else if (star != pattern.npos) {
      p = star + 1;
      n = ++retry;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*')
    ++p;

  return p == pattern.size();
}

/// Brace choices are literal filename alternatives, not shell expressions.
std::vector<std::string> alternatives(const std::string &pattern) {
  std::vector<std::string> result{pattern};
  for (size_t index = 0; index < result.size();) {
    const auto current = result[index];
    const auto open = current.find('{');
    const auto close = current.find('}');
    if (open == current.npos) {
      if (close != current.npos)
        throw std::runtime_error("unmatched source-pattern brace: " + pattern);
      ++index;
      continue;
    }

    if (close == current.npos || close < open ||
        current.find('{', open + 1) < close)
      throw std::runtime_error("invalid source-pattern braces: " + pattern);

    std::vector<std::string> expanded;
    for (auto begin = open + 1; begin <= close;) {
      const auto comma = current.find(',', begin);
      const auto end = std::min(comma, close);
      if (begin == end)
        throw std::runtime_error("empty source-pattern alternative: " +
                                 pattern);
      auto choice = current.substr(0, open);
      choice.append(current, begin, end - begin);
      choice.append(current, close + 1, current.npos);
      expanded.push_back(std::move(choice));
      if (expanded.size() > maximum_pattern_variants)
        throw std::runtime_error("too many source-pattern alternatives");
      if (end == close)
        break;
      begin = end + 1;
    }
    if (result.size() + expanded.size() - 1 > maximum_pattern_variants)
      throw std::runtime_error("too many source-pattern alternatives");

    result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
    result.insert(result.end(), expanded.begin(), expanded.end());
  }
  return result;
}

/// Compare components so a sibling with the same prefix is not excluded.
bool contains(const fs::path &parent, const fs::path &child) {
  return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end())
             .first == parent.end();
}

/// Ignore repository metadata and recognized Clutch/CMake output trees.
bool excluded(const fs::path &directory, const fs::path &build) {
  const auto name = directory.filename();
  return contains(build, directory) || name == ".git" ||
         name == layout::environment ||
         fs::exists(directory / layout::owner_marker) ||
         fs::exists(directory / "CMakeCache.txt");
}
}  // namespace

SourceSelection expand_sources(const fs::path &root,
                               const std::vector<std::string> &patterns,
                               const fs::path &build,
                               const bool require_matches) {
  std::set<fs::path> compiled, additional;
  std::set<fs::path> appended;
  std::map<fs::path, std::vector<fs::path>> directory_files;
  size_t scanned = 0;
  const auto append = [&](const fs::path &path) {
    if (!appended.insert(path).second)
      return;

    const auto canonical = fs::canonical(path);
    const auto suffix = canonical.extension().u8string();
    const bool source =
        std::find(source_extensions.begin(), source_extensions.end(), suffix) !=
        source_extensions.end();
    (source ? compiled : additional).insert(canonical);
  };
  for (const auto &pattern : patterns) {
    if (pattern.empty())
      throw std::runtime_error("source pattern must not be empty");

    bool found = false;
    for (const auto &variant : alternatives(pattern)) {
      const auto path = root / fs::u8path(variant);
      const auto filename = path.filename().u8string();
      if (filename.find_first_of("*?") == filename.npos) {
        if (path.u8string().find_first_of("*?") != std::string::npos)
          throw std::runtime_error("source wildcards belong in the filename: " +
                                   pattern);
        if (fs::is_regular_file(path)) {
          append(path);
          found = true;
        }
        continue;
      }

      auto folder = path.parent_path();
      if (folder.filename() == "**")
        folder = folder.parent_path();
      if (folder.u8string().find_first_of("*?{}") != std::string::npos)
        throw std::runtime_error("source wildcards belong in the filename: " +
                                 pattern);
      if (!fs::is_directory(folder))
        continue;

      folder = fs::absolute(folder).lexically_normal();
      if (fs::canonical(folder) != folder)
        continue;
      if (excluded(folder, build))
        continue;

      // Brace alternatives and overlapping selections share one traversal.
      // Count inspected entries rather than charging each filename filter for
      // the same filesystem work. Keep this cache local to configuration so a
      // later configure still discovers added/removed files.
      if (!directory_files.count(folder)) {
        auto &files = directory_files[folder];
        for (auto it = fs::recursive_directory_iterator(folder);
             it != fs::recursive_directory_iterator();
             ++it) {
          // Do not follow directory links or Windows junction redirects.
          if (it->is_directory()) {
            if (it->is_symlink() || fs::canonical(it->path()) != it->path() ||
                excluded(it->path(), build))
              it.disable_recursion_pending();
            continue;
          }

          if (++scanned > maximum_scanned_files)
            throw std::runtime_error("source-pattern scan exceeds file limit");
          if (it->is_regular_file())
            files.push_back(it->path());
        }
      }
      for (const auto &file : directory_files.at(folder))
        if (matches(filename, file.filename().u8string())) {
          append(file);
          found = true;
        }
    }
    if (!found && require_matches)
      throw std::runtime_error("source pattern matched no files: " + pattern);
  }
  return {{compiled.begin(), compiled.end()},
          {additional.begin(), additional.end()}};
}
}  // namespace clutch
