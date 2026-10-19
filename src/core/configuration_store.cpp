/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Serialize configured targets and reject missing, stale or moved state.
 */

#include "configuration_store.hpp"

#include "core/build_directory.hpp"
#include "core/dependencies/git.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/workspace/inputs.hpp"
#include "platform/files.hpp"
#include <toml.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace clutch {
namespace {
// Increment this version when persisted graph fields or semantics change.
// Version 10 retains noncompiled files selected by source patterns.
// Older resolved commands require explicit reconfiguration.
constexpr auto configuration_schema_version = 10;

/// Serialize paths as UTF-8 strings and preserve ordinary string argument
/// values.
template <class T>
toml::array values(const std::vector<T> &items) {
  toml::array result;
  for (const auto &item : items) {
    if constexpr (std::is_same_v<T, fs::path>)
      result.push_back(item.u8string());
    else
      result.push_back(item);
  }

  return result;
}

/// Read a required saved string, reporting the corrupt field by name.
std::string read_string(const toml::table &table, const char *const key) {
  if (const auto value = table[key].value<std::string>(); value)
    return *value;

  throw std::runtime_error(std::string("invalid saved configuration field: ") +
                           key);
}

/// Copy an argument/string array and reject any unexpected element type.
std::vector<std::string> read_strings(const toml::table &table,
                                      const char *const key) {
  if (const auto *const array = table[key].as_array(); array) {
    std::vector<std::string> result;
    for (const auto &item : *array) {
      if (const auto value = item.value<std::string>(); value)
        result.push_back(*value);
      else
        throw std::runtime_error(
            "saved configuration array contains a non-string");
    }

    return result;
  }

  throw std::runtime_error(std::string("invalid saved configuration array: ") +
                           key);
}

/// Borrow a nonempty array from saved TOML; the owning document must outlive
/// the returned reference. No reference to the diagnostic text escapes.
const toml::array &read_nonempty_array(const toml::node *const value,
                                       const std::string_view error) {
  if (const auto *const array = value ? value->as_array() : nullptr;
      array && !array->empty())
    return *array;

  throw std::runtime_error(std::string(error));
}

/// Require a saved target/unit record before interpreting its individual
/// fields.
const toml::table &read_table(const toml::node &value,
                              const char *const error) {
  if (const auto *const table = value.as_table(); table)
    return *table;

  throw std::runtime_error(error);
}

/// Reconstruct paths from the same host-specific spelling used when saving.
std::vector<fs::path> read_paths(const toml::table &table,
                                 const char *const key) {
  std::vector<fs::path> result;
  for (const auto &value : read_strings(table, key))
    result.push_back(fs::u8path(value));

  return result;
}
}  // namespace

/// Persist resolved commands so builds can execute without repeating
/// configuration.
void save_configuration(const Graph &graph, const Options &options) {
  const auto &project = graph.project;
  toml::table data{
      {"schema", configuration_schema_version},
      {"profile", options.release ? "release" : "debug"},
      {"manifest", input_signature(project.root / layout::manifest)},
      {"root", project.root.u8string()},
      {"build", project.build.u8string()},
      {"output", project.output.u8string()},
      {"name", project.name},
      {"language", project.language},
      {"c_std", project.c_standard},
      {"cpp_std", project.cxx_standard},
      {"optimize", project.optimize},
      {"warnings", project.warnings},
      {"runtime", project.runtime},
      {"sources", values(project.sources)},
      {"includes", values(project.includes)},
      {"defines", values(project.defines)},
      {"generator", graph.backend.name},
      {"build_tool", graph.backend.executable},
      {"cc", graph.toolchain.cc},
      {"cxx", graph.toolchain.cxx},
      {"msvc", graph.toolchain.msvc}};

  data.insert("workspace", project.is_workspace);
  toml::array inputs, projects;
  for (const auto &[path, signature] : project.inputs)
    inputs.push_back(
        toml::table{{"path", path.u8string()}, {"signature", signature}});

  for (const auto &[name, version, root] : project.projects)
    projects.push_back(toml::table{{"name", name},
                                   {"version", version},
                                   {"root", root.u8string()}});

  data.insert("inputs", std::move(inputs));
  data.insert("projects", std::move(projects));
  toml::array foreign_builds;
  for (const auto &build : project.foreign_builds)
    foreign_builds.push_back(toml::table{
        {"id", build.id},
        {"directory", build.directory.u8string()},
        {"arguments", values(build.arguments)},
        {"artifacts", values(build.artifacts)},
        {"compilation_database", build.compilation_database.u8string()}});
  data.insert("foreign_builds", std::move(foreign_builds));

  // Retain complete argument arrays, including the executable, for each action.
  toml::array targets;
  for (const auto &target : graph.targets) {
    toml::array units, prefixes;
    for (const auto &prefix : target.precompiled_headers)
      prefixes.push_back(
          toml::table{{"language", prefix.language},
                      {"driver", prefix.driver},
                      {"header", prefix.header.u8string()},
                      {"ide_header", prefix.ide_header.u8string()},
                      {"companion", prefix.companion_object.u8string()},
                      {"headers", values(prefix.headers)},
                      {"source", prefix.creator.source.u8string()},
                      {"object", prefix.creator.object.u8string()},
                      {"arguments", values(prefix.creator.arguments)}});
    for (const auto &[source, object, arguments] : target.units)
      units.push_back(toml::table{{"source", source.u8string()},
                                  {"object", object.u8string()},
                                  {"arguments", values(arguments)}});

    std::vector<std::string> dependencies, link_dependencies;
    for (const auto index : target.dependencies)
      dependencies.push_back(graph.targets.at(index).name);
    for (const auto index : target.link_dependencies)
      link_dependencies.push_back(graph.targets.at(index).name);
    targets.push_back(toml::table{
        {"name", target.name},
        {"project", target.project_name},
        {"local_name", target.local_name},
        {"source_root", target.source_root.u8string()},
        {"c_std", target.c_standard},
        {"cpp_std", target.cxx_standard},
        {"type", target_type_name(target.type)},
        {"test", target.is_test},
        {"default", target.build_by_default},
        {"import_library", target.import_library.u8string()},
        {"dependencies", values(dependencies)},
        {"link_dependencies", values(link_dependencies)},
        {"includes", values(target.includes)},
        {"defines", values(target.defines)},
        {"foreign_dependencies", values(target.foreign_dependencies)},
        {"link_inputs", values(target.link_inputs)},
        {"runtime_libraries", values(target.runtime_libraries)},
        {"additional_files", values(target.additional_files)},
        {"output", target.output.u8string()},
        {"link", values(target.link)},
        {"pch", std::move(prefixes)},
        {"units", std::move(units)}});
  }

  data.insert("targets", std::move(targets));
  std::ostringstream text;
  text << data << '\n';
  write_if_changed(project.build / layout::configuration, text.str());
}

/// Restore only current, owned, location-matching configuration and revalidate
/// it.
Graph load_configuration(const Options &options) {
  const auto root = fs::canonical(options.root);
  const auto directory = build_directory(options);
  validate_build_directory(root, directory, true);

  const auto path = directory / layout::configuration;
  const auto configure_hint = "run clutch \"" + root.u8string() + "\"" +
                              (options.release ? " --release" : "");
  if (!fs::is_regular_file(path))
    throw std::runtime_error("project is not configured; " + configure_hint);

  // Reject stale formats/manifests before accepting any cached command vectors.
  const auto data = toml::parse_file(path.u8string());
  if (data["schema"].value_or(0) != configuration_schema_version)
    throw std::runtime_error("saved configuration version is unsupported; " +
                             configure_hint);

  if (read_string(data, "manifest") != input_signature(root / layout::manifest))
    throw std::runtime_error("clutch.toml changed since configuration; " +
                             configure_hint);

  // Saved absolute paths intentionally bind a graph to one location and
  // profile.
  Graph graph;
  auto &project = graph.project;
  for (const auto &input :
       read_nonempty_array(data.get("inputs"),
                           "saved configuration has no inputs")) {
    const auto &table = read_table(input, "invalid saved configuration input");
    project.inputs.push_back({fs::u8path(read_string(table, "path")),
                              read_string(table, "signature")});
  }
  validate_inputs(project.inputs);

  project.is_workspace = data["workspace"].value_or(false);
  for (const auto &record :
       read_nonempty_array(data.get("projects"),
                           "saved configuration has no projects")) {
    const auto &table = read_table(record, "invalid saved project");
    project.projects.push_back({read_string(table, "name"),
                                read_string(table, "version"),
                                fs::u8path(read_string(table, "root"))});
  }
  project.root = fs::u8path(read_string(data, "root"));
  project.packages = dependencies::read_pins(root);
  project.build = fs::u8path(read_string(data, "build"));
  if (project.root != root || project.build != directory ||
      read_string(data, "profile") != (options.release ? "release" : "debug"))
    throw std::runtime_error(
        "saved configuration belongs to another location/profile; " +
        configure_hint);

  project.output = fs::u8path(read_string(data, "output"));
  project.name = read_string(data, "name");
  project.language = read_string(data, "language");
  project.c_standard = read_string(data, "c_std");
  project.cxx_standard = read_string(data, "cpp_std");
  project.optimize = read_string(data, "optimize");
  project.warnings = read_string(data, "warnings");
  project.runtime = read_string(data, "runtime");
  project.sources = read_paths(data, "sources");
  project.includes = read_paths(data, "includes");
  project.defines = read_strings(data, "defines");
  project.generator = read_string(data, "generator");
  graph.backend = {project.generator, read_string(data, "build_tool")};
  graph.toolchain = {read_string(data, "cc"),
                     read_string(data, "cxx"),
                     data["msvc"].value_or(false)};
  const auto *const foreign_builds = data["foreign_builds"].as_array();
  if (!foreign_builds)
    throw std::runtime_error("invalid saved foreign build actions");
  for (const auto &entry : *foreign_builds) {
    const auto &record =
        read_table(entry, "invalid saved foreign build action");
    auto build =
        dependencies::ForeignBuild{read_string(record, "id"),
                                   fs::u8path(read_string(record, "directory")),
                                   read_strings(record, "arguments"),
                                   read_paths(record, "artifacts")};
    build.compilation_database =
        fs::u8path(read_string(record, "compilation_database"));
    if (build.arguments.empty() || !build.directory.is_absolute())
      throw std::runtime_error("invalid saved foreign command");
    project.foreign_builds.push_back(std::move(build));
  }

  // Rebuild application/test commands without discovering sources or tools
  // again.
  const auto &targets = read_nonempty_array(
      data.get("targets"),
      "saved configuration has no targets; " + configure_hint);

  for (const auto &value : targets) {
    const auto &table = read_table(value, "invalid saved target");

    Target target;
    target.name = read_string(table, "name");
    target.project_name = read_string(table, "project");
    target.local_name = read_string(table, "local_name");
    target.source_root = fs::u8path(read_string(table, "source_root"));
    target.c_standard = read_string(table, "c_std");
    target.cxx_standard = read_string(table, "cpp_std");
    target.output = fs::u8path(read_string(table, "output"));
    target.type = parse_target_type(read_string(table, "type"));
    if (!table["test"].is_boolean() || !table["default"].is_boolean())
      throw std::runtime_error("invalid saved target selection flags");
    target.is_test = table["test"].value_or(false);
    target.build_by_default = table["default"].value_or(false);
    target.import_library = fs::u8path(read_string(table, "import_library"));
    target.includes = read_paths(table, "includes");
    target.defines = read_strings(table, "defines");
    target.foreign_dependencies = read_strings(table, "foreign_dependencies");
    target.link_inputs = read_paths(table, "link_inputs");
    target.runtime_libraries = read_paths(table, "runtime_libraries");
    target.additional_files = read_paths(table, "additional_files");
    target.link = read_strings(table, "link");
    const auto *const prefixes = table["pch"].as_array();
    if (!prefixes)
      throw std::runtime_error("invalid saved PCH records");

    for (const auto &prefix : *prefixes) {
      const auto &record = read_table(prefix, "invalid saved PCH record");
      PrecompiledHeader restored;
      restored.language = read_string(record, "language");
      restored.driver = read_string(record, "driver");
      restored.header = fs::u8path(read_string(record, "header"));
      restored.ide_header = fs::u8path(read_string(record, "ide_header"));
      restored.companion_object = fs::u8path(read_string(record, "companion"));
      restored.headers = read_paths(record, "headers");
      restored.creator = {fs::u8path(read_string(record, "source")),
                          fs::u8path(read_string(record, "object")),
                          read_strings(record, "arguments")};
      if ((restored.language != "c" && restored.language != "cpp") ||
          (restored.driver != "msvc" && restored.driver != "gcc" &&
           restored.driver != "clang") ||
          restored.headers.empty() || restored.creator.arguments.empty())
        throw std::runtime_error("invalid saved PCH command");

      target.precompiled_headers.push_back(std::move(restored));
    }
    const auto &units = read_nonempty_array(table.get("units"),
                                            "invalid saved target commands");
    if (target.link.empty())
      throw std::runtime_error("invalid saved target commands");

    for (const auto &unit : units) {
      const auto &record = read_table(unit, "invalid saved compilation unit");

      Unit resolved{fs::u8path(read_string(record, "source")),
                    fs::u8path(read_string(record, "object")),
                    read_strings(record, "arguments")};
      if (resolved.arguments.empty())
        throw std::runtime_error("saved compilation command is empty");

      target.units.push_back(std::move(resolved));
    }

    graph.targets.push_back(std::move(target));
  }

  size_t target_index = 0;
  for (const auto &value : targets) {
    const auto &table = read_table(value, "invalid saved target");
    const auto restore = [&](const char *const key,
                             std::vector<size_t> &indices) {
      for (const auto &name : read_strings(table, key)) {
        const auto found = std::find_if(graph.targets.begin(),
                                        graph.targets.end(),
                                        [&](const Target &target) {
                                          return target.name == name;
                                        });
        if (found == graph.targets.end())
          throw std::runtime_error("unknown saved target dependency: " + name);
        indices.push_back(static_cast<size_t>(found - graph.targets.begin()));
      }
    };
    restore("dependencies", graph.targets[target_index].dependencies);
    restore("link_dependencies", graph.targets[target_index].link_dependencies);
    ++target_index;
  }

  // Loading persisted data must enforce the same graph invariants as
  // configuration.
  validate_build_graph(graph);
  return graph;
}
}  // namespace clutch
