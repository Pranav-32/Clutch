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

#include "core/pch/headers.hpp"
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
std::vector<std::string> strings(const toml::table &table,
                                 const char *const key,
                                 const char *const context = "target") {
  std::vector<std::string> result;
  if (const auto *const node = table.get(key)) {
    if (const auto *const array = node->as_array(); array) {
      for (const auto &value : *array) {
        if (const auto text = value.value<std::string>())
          result.push_back(*text);
        else
          throw std::runtime_error(std::string(context) + "." + key +
                                   " must contain strings");
      }
    } else {
      throw std::runtime_error(std::string(context) + "." + key +
                               " must be an array of strings");
    }
  }
  return result;
}

/// Resolve only existing directories; generators consume canonical paths.
std::vector<fs::path> directories(const toml::table &table,
                                  const char *const key,
                                  const fs::path &root) {
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
target(const toml::table &table, const Project &project, const bool test) {
  const auto &root = project.root;
  const std::set<std::string> allowed{"name",
                                      "sources",
                                      "type",
                                      "include_dirs",
                                      "public_include_dirs",
                                      "defines",
                                      "public_defines",
                                      "links",
                                      "public_links",
                                      "default",
                                      "pch"};
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

  if (const auto *const value = table.get("type"); value && !value->is_string())
    throw std::runtime_error("target.type must be a string");

  result.type =
      parse_target_type(table["type"].value_or(std::string("executable")));
  result.is_test = test;
  result.build_by_default = !test;
  if (test && result.type != TargetType::Executable)
    throw std::runtime_error("test targets must be executable");

  if (const auto *const value = table.get("default")) {
    if (!value->is_boolean() || test)
      throw std::runtime_error(
          "target.default must be a boolean and is not supported for tests");

    result.build_by_default = value->as_boolean()->get();
  }

  auto selected =
      expand_sources(root, strings(table, "sources"), project.build);
  result.sources = std::move(selected.sources);
  result.additional_files = std::move(selected.additional_files);
  // The project's executable may receive application defaults after automatic
  // discovery has excluded files owned by other explicit targets.
  if (result.sources.empty() &&
      (test || result.type != TargetType::Executable ||
       result.name != project.name || project.sources.empty()))
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
  const auto headers = pch::read(table.get("pch"), "target.pch");
  result.pch_c = pch::paths(headers.c, root);
  result.pch_cpp = pch::paths(headers.cpp, root);
  return result;
}
}  // namespace

void load_manifest_targets(const toml::table &document, Project &project) {
  std::set<std::string> names;
  if (!document.contains("target"))
    names.insert(project.name);

  for (const auto *const key : {"target", "test"}) {
    if (const auto *const node = document.get(key)) {
      const auto *const array = node->as_array();
      if (!array || (std::string(key) == "target" && array->empty()))
        throw std::runtime_error(std::string(key) +
                                 " must be a nonempty array of tables");

      for (const auto &value : *array) {
        const auto *const table = value.as_table();
        if (!table)
          throw std::runtime_error(std::string(key) + " must contain tables");

        auto spec = target(*table, project, std::string(key) == "test");
        if (!names.insert(spec.name).second)
          throw std::runtime_error("duplicate target/test name: " + spec.name);

        (spec.is_test ? project.tests : project.targets)
            .push_back(std::move(spec));
      }
    }
  }
}

void complete_application_target(const toml::table &document,
                                 Project &project) {
  const toml::table empty;
  const auto *const build = document["build"].as_table();
  const auto links = strings(build ? *build : empty, "links", "build");
  // Preserve implicit discovery and its object layout when no target mixing or
  // application link settings are requested.
  if (project.targets.empty() && links.empty())
    return;

  const bool sources_requested =
      document["project"]["entry"] || document["build"]["sources"];
  if (!sources_requested && links.empty())
    return;

  if (const auto found = std::find_if(project.targets.begin(),
                                      project.targets.end(),
                                      [&](const TargetSpec &spec) {
                                        return spec.name == project.name;
                                      });
      found != project.targets.end()) {
    if (found->type != TargetType::Executable)
      throw std::runtime_error(
          "application defaults conflict with library target '" + project.name +
          "'; use distinct target names");

    found->sources.insert(found->sources.end(),
                          project.sources.begin(),
                          project.sources.end());
    found->additional_files.insert(found->additional_files.end(),
                                   project.additional_files.begin(),
                                   project.additional_files.end());
    std::sort(found->additional_files.begin(), found->additional_files.end());
    found->additional_files.erase(std::unique(found->additional_files.begin(),
                                              found->additional_files.end()),
                                  found->additional_files.end());
    found->links.insert(found->links.end(), links.begin(), links.end());
    std::sort(found->sources.begin(), found->sources.end());
    found->sources.erase(
        std::unique(found->sources.begin(), found->sources.end()),
        found->sources.end());
    if (found->sources.empty())
      throw std::runtime_error("application has no sources: " + project.name);
    return;
  }

  if (project.sources.empty())
    throw std::runtime_error("build.links needs project.entry, build.sources "
                             "or an executable target named '" +
                             project.name + "'");

  if (std::any_of(project.tests.begin(),
                  project.tests.end(),
                  [&](const TargetSpec &spec) {
                    return spec.name == project.name;
                  }))
    throw std::runtime_error("duplicate target/test name: " + project.name);

  TargetSpec application;
  application.name = project.name;
  application.sources = project.sources;
  application.additional_files = project.additional_files;
  application.links = links;
  project.targets.insert(project.targets.begin(), std::move(application));
}
}  // namespace clutch
