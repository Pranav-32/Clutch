/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Preserve deterministic include order and macro precedence across
 * scopes.
 */
#include "requirements.hpp"

#include <algorithm>
#include <map>

namespace clutch {
std::vector<fs::path> merge_includes(const std::vector<fs::path> &defaults,
                                     const std::vector<fs::path> &local) {
  std::vector<fs::path> result;
  for (const auto *const group : {&local, &defaults})
    for (const auto &path : *group)
      if (std::find(result.begin(), result.end(), path) == result.end())
        result.push_back(path);

  return result;
}

std::vector<std::string> merge_defines(const std::vector<std::string> &defaults,
                                       const std::vector<std::string> &local) {
  std::vector<std::string> result;
  std::map<std::string, size_t> positions;
  for (const auto *const group : {&defaults, &local}) {
    for (const auto &value : *group) {
      const auto name = value.substr(0, value.find_first_of("=("));
      if (const auto found = positions.find(name); found != positions.end())
        result[found->second] = value;
      else {
        positions.emplace(name, result.size());
        result.push_back(value);
      }
    }
  }

  return result;
}
}  // namespace clutch
