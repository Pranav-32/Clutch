/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Reject ambiguous selectors, malformed requirements and unsafe aliases.
 */

#include "manifest.hpp"
#include "core/pch/headers.hpp"
#include "version.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clutch::dependencies {
namespace {
// Full object IDs support Git's SHA-1 and SHA-256 repository formats.
constexpr size_t sha1_digits = 40, sha256_digits = 64;

/// Read a strict optional string instead of silently accepting a wrong type.
std::string text(const toml::table &table, const char *const key) {
  if (const auto *const node = table.get(key)) {
    if (const auto value = node->value<std::string>(); value && !value->empty())
      return *value;

    throw std::runtime_error(std::string("dependency.") + key +
                             " must be a nonempty string");
  }

  return {};
}

/// Preserve the order of explicit usage requirements.
std::vector<std::string> strings(const toml::table &table,
                                 const char *const key) {
  std::vector<std::string> result;
  if (const auto *const node = table.get(key)) {
    if (const auto *const values = node->as_array(); values) {
      for (const auto &value : *values) {
        if (const auto item = value.value<std::string>();
            item && !item->empty())
          result.push_back(*item);
        else
          throw std::runtime_error(std::string("dependency.") + key +
                                   " must contain nonempty strings");
      }
    } else
      throw std::runtime_error(std::string("dependency.") + key +
                               " must be an array");
  }

  return result;
}
}  // namespace

std::vector<Requirement> read_requirements(const toml::table &document,
                                           const fs::path &root) {
  std::vector<Requirement> result;
  const auto *const node = document.get("dependencies");
  if (!node)
    return result;

  const auto *const table = node->as_table();
  if (!table)
    throw std::runtime_error("dependencies must be a table of named imports");
  const std::set<std::string> allowed{"git",           "tag",
                                      "branch",        "rev",
                                      "version",       "target",
                                      "build_system",  "include_dirs",
                                      "defines",       "pch",
                                      "path",          "system",
                                      "vcpkg",         "root",
                                      "triplet",       "baseline",
                                      "targets",       "components",
                                      "features",      "libraries",
                                      "runtime",       "build_arguments",
                                      "cmake_options", "cmake_package"};
  for (const auto &[alias, value] : *table) {
    Requirement requirement;
    requirement.alias = alias.str();
    if (requirement.alias.empty() ||
        !std::all_of(requirement.alias.begin(),
                     requirement.alias.end(),
                     [](const unsigned char character) {
                       return (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') ||
                              character == '_' || character == '-';
                     }))
      throw std::runtime_error(
          "dependency aliases must contain letters, digits, '_' or '-'");
    const auto *const settings = value.as_table();
    if (!settings)
      throw std::runtime_error("dependency '" + requirement.alias +
                               "' must be a table");
    for (const auto &[key, setting] : *settings) {
      (void)setting;
      if (!allowed.count(std::string(key.str())))
        throw std::runtime_error("unknown dependency key: " +
                                 std::string(key.str()));
    }

    requirement.git = text(*settings, "git");
    requirement.path = text(*settings, "path");
    requirement.system = text(*settings, "system");
    requirement.vcpkg = text(*settings, "vcpkg");
    const auto sources = !requirement.git.empty() + !requirement.path.empty() +
                         !requirement.system.empty() +
                         !requirement.vcpkg.empty();
    if (sources != 1)
      throw std::runtime_error(
          "dependency must select exactly one of git, path, system or vcpkg");

    if (!requirement.path.empty()) {
      requirement.provider = "path";
      requirement.path =
          fs::canonical(root / fs::u8path(requirement.path)).generic_u8string();
      if (!fs::is_directory(fs::u8path(requirement.path)))
        throw std::runtime_error("dependency.path must name a directory");
    } else if (!requirement.system.empty())
      requirement.provider = "system";
    else if (!requirement.vcpkg.empty())
      requirement.provider = "vcpkg";

    if (requirement.provider == "git") {
      if (requirement.git.empty() || requirement.git.front() == '-' ||
          std::any_of(requirement.git.begin(),
                      requirement.git.end(),
                      [](const unsigned char character) {
                        return character < ' ' || character == 127U;
                      }))
        throw std::runtime_error(
            "dependency.git must be a nonempty Git URL or repository path");
      if (requirement.git.rfind("http://", 0) == 0 ||
          requirement.git.rfind("https://", 0) == 0) {
        const auto authority_start =
            requirement.git.find("://") + std::string_view("://").size();
        const auto authority_end = requirement.git.find('/', authority_start);
        const auto credentials = requirement.git.find('@', authority_start);
        if (credentials != std::string::npos &&
            (authority_end == std::string::npos || credentials < authority_end))
          throw std::runtime_error("use a Git credential helper instead of "
                                   "embedding HTTP credentials "
                                   "in dependency URLs");
      }

      const auto path = fs::u8path(requirement.git);
      if (path.is_absolute() || requirement.git.find(':') == std::string::npos)
        requirement.git = fs::canonical(root / path).generic_u8string();
    }
    requirement.tag = text(*settings, "tag");
    requirement.branch = text(*settings, "branch");
    requirement.revision = text(*settings, "rev");
    requirement.version = text(*settings, "version");
    requirement.target = text(*settings, "target");
    for (const auto *const selector : {&requirement.tag, &requirement.branch})
      if (!selector->empty() && (selector->front() == '-' ||
                                 std::any_of(selector->begin(),
                                             selector->end(),
                                             [](const unsigned char c) {
                                               return c <= ' ' || c == 127U;
                                             })))
        throw std::runtime_error("dependency tag/branch must be a Git ref "
                                 "without whitespace or leading '-'");

    if (const auto system = text(*settings, "build_system"); !system.empty())
      requirement.build_system = system;
    if (requirement.build_system != "auto" &&
        requirement.build_system != "clutch" &&
        requirement.build_system != "header-only" &&
        requirement.build_system != "cmake" &&
        requirement.build_system != "make")
      throw std::runtime_error("dependency build_system must be auto, clutch, "
                               "header-only, cmake or make");
    const auto selectors = !requirement.tag.empty() +
                           !requirement.branch.empty() +
                           !requirement.revision.empty();
    if (selectors > 1)
      throw std::runtime_error(
          "dependency must select only one of tag, branch or rev");
    if (selectors && requirement.provider != "git")
      throw std::runtime_error(
          "tag, branch and rev apply only to Git dependencies");
    if (!requirement.revision.empty() &&
        ((requirement.revision.size() != sha1_digits &&
          requirement.revision.size() != sha256_digits) ||
         !std::all_of(requirement.revision.begin(),
                      requirement.revision.end(),
                      [](const unsigned char character) {
                        return std::isxdigit(character) != 0;
                      })))
      throw std::runtime_error(
          "dependency.rev must be a full 40- or 64-digit Git commit ID");
    std::transform(requirement.revision.begin(),
                   requirement.revision.end(),
                   requirement.revision.begin(),
                   [](const unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    if (requirement.provider == "git" || requirement.provider == "path")
      (void)satisfies("0.0.0", requirement.version);

    requirement.root = text(*settings, "root");
    if (!requirement.root.empty())
      requirement.root =
          fs::canonical(root / fs::u8path(requirement.root)).generic_u8string();
    requirement.triplet = text(*settings, "triplet");
    requirement.baseline = text(*settings, "baseline");
    requirement.cmake_package = text(*settings, "cmake_package");
    requirement.targets = strings(*settings, "targets");
    requirement.components = strings(*settings, "components");
    requirement.features = strings(*settings, "features");
    requirement.libraries = strings(*settings, "libraries");
    requirement.runtime = strings(*settings, "runtime");
    requirement.build_arguments = strings(*settings, "build_arguments");
    if (requirement.provider == "system" &&
        requirement.build_system != "auto" &&
        requirement.build_system != "cmake")
      throw std::runtime_error(
          "system dependencies accept build_system auto or cmake");
    if (!requirement.root.empty() && requirement.provider != "system" &&
        requirement.provider != "vcpkg")
      throw std::runtime_error("dependency.root applies to installed packages "
                               "or a local vcpkg catalog repository");
    if (!requirement.cmake_package.empty() && requirement.provider != "system")
      throw std::runtime_error(
          "dependency.cmake_package applies to installed packages");
    if (!requirement.components.empty() && requirement.provider != "system")
      throw std::runtime_error(
          "dependency.components applies to installed packages");
    if ((!requirement.features.empty() || !requirement.triplet.empty()) &&
        requirement.provider != "vcpkg")
      throw std::runtime_error(
          "features and triplet do not apply to Git/local/system imports");
    if (!requirement.target.empty() && !requirement.targets.empty())
      throw std::runtime_error(
          "dependency must select target or targets, not both");
    if (requirement.provider == "vcpkg" &&
        (requirement.baseline.size() != sha1_digits ||
         !std::all_of(requirement.baseline.begin(),
                      requirement.baseline.end(),
                      [](const unsigned char c) {
                        return std::isxdigit(c) != 0;
                      })))
      throw std::runtime_error(
          "vcpkg catalog imports require a 40-digit registry baseline");
    if (!requirement.baseline.empty() && requirement.provider != "vcpkg")
      throw std::runtime_error(
          "dependency.baseline applies only to vcpkg catalog imports");
    std::transform(requirement.baseline.begin(),
                   requirement.baseline.end(),
                   requirement.baseline.begin(),
                   [](const unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });

    if (const auto *const options = settings->get("cmake_options")) {
      const auto *const values = options->as_table();
      if (!values)
        throw std::runtime_error(
            "dependency.cmake_options must be a table of string values");
      for (const auto &[key, option] : *values)
        if (const auto item = option.value<std::string>(); item)
          requirement.cmake_options.emplace_back(key.str(), *item);
        else
          throw std::runtime_error(
              "dependency.cmake_options values must be strings");
    }
    requirement.include_dirs = strings(*settings, "include_dirs");
    if (requirement.provider == "system") {
      const auto prefix =
          requirement.root.empty() ? root : fs::u8path(requirement.root);
      for (auto &directory : requirement.include_dirs)
        directory =
            fs::canonical(prefix / fs::u8path(directory)).generic_u8string();
      for (auto &library : requirement.libraries) {
        const auto path = fs::u8path(library);
        if (path.has_parent_path() || fs::is_regular_file(prefix / path)) {
          const auto file = fs::canonical(prefix / path);
          if (!fs::is_regular_file(file))
            throw std::runtime_error("system library path is not a file: " +
                                     library);
          library = file.generic_u8string();
        }
      }
      if (!requirement.libraries.empty() &&
          (!requirement.target.empty() || !requirement.targets.empty()))
        throw std::runtime_error("system dependencies select imported targets "
                                 "or explicit libraries, not both");
    }
    requirement.defines = strings(*settings, "defines");
    const auto headers = pch::read(settings->get("pch"), "dependency.pch");
    requirement.pch = headers.cpp;
    requirement.pch_c = headers.c;
    if (requirement.provider == "system") {
      // Preserve the declaring package's frame when a workspace/native import
      // is later composed into a different root project's graph.
      const auto prefix =
          requirement.root.empty() ? root : fs::u8path(requirement.root);
      for (auto *const paths :
           {&requirement.runtime, &requirement.pch, &requirement.pch_c})
        for (auto &path : *paths)
          path = fs::canonical(prefix / fs::u8path(path)).generic_u8string();
    }

    result.push_back(std::move(requirement));
  }

  return result;
}
}  // namespace clutch::dependencies
