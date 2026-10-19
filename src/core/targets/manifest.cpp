/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Validate target fields, source sets and public/private usage
 * requirements.
 */
#include "manifest.hpp"

#include "core/project.hpp"
#include "core/source_files.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace clutch {
namespace {
/// Keep names safe for build aliases, native IDE identities and artifact names.
bool valid_name(const std::string &name) {
  return !name.empty() &&
         std::all_of(name.begin(), name.end(), [](const unsigned char value) {
           return std::isalnum(value) || value == '_' || value == '-';
         });
}

/// Read arrays without implicit string/boolean conversion by the TOML library.
std::vector<std::string> strings(const toml::table &table, const char *key) {
  std::vector<std::string> result;
  if (const auto *node = table.get(key)) {
    const auto *array = node->as_array();
    if (!array)
      throw std::runtime_error(std::string("target.") + key +
                               " must be an array of strings");

    for (const auto &value : *array) {
      if (const auto text = value.value<std::string>())
        result.push_back(*text);
      else
        throw std::runtime_error(std::string("target.") + key +
                                 " must contain strings");
    }
  }
  return result;
}

/// Resolve only existing directories; generators consume canonical paths.
std::vector<fs::path>
directories(const toml::table &table, const char *key, const fs::path &root) {
  std::vector<fs::path> result;
  for (const auto &value : strings(table, key)) {
    const auto path = fs::canonical(root / fs::u8path(value));
    if (!fs::is_directory(path))
      throw std::runtime_error(std::string("target.") + key +
                               " contains a non-directory");
    result.push_back(path);
  }
  return result;
}

/// Validate one target's schema before filling defaults or discovering tools.
TargetSpec
target(const toml::table &table, const fs::path &root, const bool test) {
  const std::set<std::string> allowed{"name",
                                      "sources",
                                      "type",
                                      "include_dirs",
                                      "public_include_dirs",
                                      "defines",
                                      "public_defines",
                                      "links",
                                      "public_links",
                                      "default"};
  for (const auto &[key, value] : table) {
    (void)value;
    if (!allowed.count(std::string(key.str())))
      throw std::runtime_error("unknown target key: " + std::string(key.str()));
  }
  TargetSpec result;
  result.name = table["name"].value_or(std::string{});
  if (!valid_name(result.name))
    throw std::runtime_error(
        "target/test.name must contain only letters, digits, '_' or '-'");

  if (const auto *value = table.get("type"); value && !value->is_string())
    throw std::runtime_error("target.type must be a string");

  result.type =
      parse_target_type(table["type"].value_or(std::string("executable")));
  result.is_test = test;
  result.build_by_default = !test;
  if (test && result.type != TargetType::Executable)
    throw std::runtime_error("test targets must be executable");

  if (const auto *value = table.get("default")) {
    if (!value->is_boolean() || test)
      throw std::runtime_error(
          "target.default must be a boolean and is not supported for tests");

    result.build_by_default = value->as_boolean()->get();
  }

  for (const auto &source : strings(table, "sources")) {
    const auto path = fs::canonical(root / fs::u8path(source));
    if (!fs::is_regular_file(path) ||
        std::find(source_extensions.begin(),
                  source_extensions.end(),
                  path.extension().u8string()) == source_extensions.end())
      throw std::runtime_error("unsupported target source: " + path.u8string());

    result.sources.push_back(path);
  }
  if (result.sources.empty())
    throw std::runtime_error(
        "target/test.sources must be a nonempty array of source files");

  std::sort(result.sources.begin(), result.sources.end());
  result.sources.erase(
      std::unique(result.sources.begin(), result.sources.end()),
      result.sources.end());
  result.includes = directories(table, "include_dirs", root);
  result.public_includes = directories(table, "public_include_dirs", root);
  result.defines = strings(table, "defines");
  result.public_defines = strings(table, "public_defines");
  result.links = strings(table, "links");
  result.public_links = strings(table, "public_links");
  return result;
}
}  // namespace

void load_manifest_targets(const toml::table &document, Project &project) {
  std::set<std::string> names;
  if (!document.contains("target"))
    names.insert(project.name);

  for (const auto *key : {"target", "test"}) {
    if (const auto *node = document.get(key)) {
      const auto *array = node->as_array();
      if (!array || (std::string(key) == "target" && array->empty()))
        throw std::runtime_error(std::string(key) +
                                 " must be a nonempty array of tables");

      for (const auto &value : *array) {
        const auto *table = value.as_table();
        if (!table)
          throw std::runtime_error(std::string(key) + " must contain tables");

        auto spec = target(*table, project.root, std::string(key) == "test");
        if (!names.insert(spec.name).second)
          throw std::runtime_error("duplicate target/test name: " + spec.name);

        (spec.is_test ? project.tests : project.targets)
            .push_back(std::move(spec));
      }
    }
  }
  if (!project.targets.empty() &&
      (document["build"]["sources"] || document["project"]["entry"]))
    throw std::runtime_error("explicit [[target]] declarations cannot be "
                             "combined with build.sources or project.entry");
}
}  // namespace clutch
