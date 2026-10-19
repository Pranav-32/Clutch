/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Encode dependency identities without ambiguous delimiter
 * concatenation.
 */

#include "dependency.hpp"
#include "core/hash_constants.hpp"
#include <iomanip>
#include <sstream>

namespace clutch::dependencies {
std::string identity(const Requirement &requirement) {
  if (requirement.provider == "git")
    return requirement.git;

  if (requirement.provider == "path")
    return "path:" + (requirement.locator.empty() ? requirement.path
                                                  : requirement.locator);

  return requirement.provider + ":" +
         (requirement.provider == "system"
              ? requirement.system
              : requirement.vcpkg + ":" + requirement.triplet);
}

std::string identity(const Package &package) {
  return package.provider == "git" ? package.git
                                   : package.provider + ":" + package.locator;
}

std::string source_key(const std::string &git) {
  auto value = hashing::fnv1a64_offset_basis;
  for (const unsigned char byte : git) {
    value ^= byte;
    value *= hashing::fnv1a64_prime;
  }

  std::ostringstream text;
  // This is a path namespace, not authentication. Every cache read also checks
  // its full Git identity and exact commit before accepting the directory.
  constexpr auto hex_width = 16;
  text << "dep-" << std::hex << std::setfill('0') << std::setw(hex_width)
       << value;
  return text.str();
}

std::string request_key(const Request &request) {
  std::string result;
  const auto &r = request.requirement;
  for (const auto *const value : {&request.owner,
                                  &r.alias,
                                  &r.git,
                                  &r.tag,
                                  &r.branch,
                                  &r.revision,
                                  &r.version})
    result += std::to_string(value->size()) + ':' + *value;
  if (r.provider != "git")
    for (const auto &value : {r.provider, identity(r), r.baseline})
      result += std::to_string(value.size()) + ':' + value;
  if (!r.vcpkg.empty())
    for (const auto &value :
         {std::string("vcpkg"), r.vcpkg, r.baseline, r.catalog_version})
      result += std::to_string(value.size()) + ':' + value;
  return result;
}
}  // namespace clutch::dependencies
