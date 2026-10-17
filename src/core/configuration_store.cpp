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
#include "core/encoding_constants.hpp"
#include "core/hash_constants.hpp"
#include "core/options.hpp"
#include "platform/files.hpp"
#include <toml.hpp>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace clutch {
namespace {
// Increment this version when persisted graph fields or semantics change.
// Version 2 requires [project] manifests and explicit warning suppression.
constexpr auto configuration_schema_version = 2;
constexpr auto hash64_hex_digits =
    static_cast<int>(sizeof(std::uint64_t) * encoding::hex_digits_per_byte);

/// Hash manifest bytes so even timestamp-preserving edits invalidate saved
/// state.
std::string manifest_signature(const fs::path &root) {
  if (std::ifstream input(root / "clutch.toml", std::ios::binary); input) {
    // This is a change detector, not a security checksum.
    auto hash = hashing::fnv1a64_offset_basis;
    for (auto current = std::istreambuf_iterator(input);
         current != decltype(current){};
         ++current) {
      hash ^= static_cast<unsigned char>(*current);
      hash *= hashing::fnv1a64_prime;
    }

    std::ostringstream text;
    text << std::hex << std::setw(hash64_hex_digits) << std::setfill('0')
        << hash;
    return text.str();
  }

  throw std::runtime_error("cannot read clutch.toml");
}

/// Serialize paths as host strings and preserve ordinary string argument
/// values.
template <class T>
toml::array values(const std::vector<T> &items) {
  toml::array result;
  for (const auto &item : items) {
    if constexpr (std::is_same_v<T, fs::path>)
      result.push_back(item.string());
    else
      result.push_back(item);
  }

  return result;
}

/// Read a required saved string, reporting the corrupt field by name.
std::string read_string(const toml::table &table, const char *key) {
  if (const auto value = table[key].value<std::string>(); value)
    return *value;

  throw std::runtime_error(std::string("invalid saved configuration field: ") +
                           key);
}

/// Copy an argument/string array and reject any unexpected element type.
std::vector<std::string> read_strings(const toml::table &table,
                                      const char *const key) {
  if (const auto *array = table[key].as_array(); array) {
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
  if (const auto *array = value ? value->as_array() : nullptr;
    array && !array->empty())
    return *array;

  throw std::runtime_error(std::string(error));
}

/// Require a saved target/unit record before interpreting its individual
/// fields.
const toml::table &read_table(const toml::node &value, const char *error) {
  if (const auto *table = value.as_table(); table)
    return *table;

  throw std::runtime_error(error);
}

/// Reconstruct paths from the same host-specific spelling used when saving.
std::vector<fs::path> read_paths(const toml::table &table,
                                 const char *const key) {
  std::vector<fs::path> result;
  for (const auto &value : read_strings(table, key))
    result.emplace_back(value);

  return result;
}
} // namespace

/// Persist resolved commands so builds can execute without repeating
/// configuration.
void save_configuration(const Graph &graph, const Options &options) {
  const auto &project = graph.project;
  toml::table data{{"schema", configuration_schema_version},
                   {"profile", options.release ? "release" : "debug"},
                   {"manifest", manifest_signature(project.root)},
                   {"root", project.root.string()},
                   {"build", project.build.string()},
                   {"output", project.output.string()},
                   {"name", project.name},
                   {"language", project.language},
                   {"c_std", project.c_standard},
                   {"cpp_std", project.cxx_standard},
                   {"optimize", project.optimize},
                   {"warnings", project.warnings},
                   {"sources", values(project.sources)},
                   {"includes", values(project.includes)},
                   {"defines", values(project.defines)},
                   {"generator", graph.backend.name},
                   {"build_tool", graph.backend.executable},
                   {"cc", graph.toolchain.cc},
                   {"cxx", graph.toolchain.cxx},
                   {"msvc", graph.toolchain.msvc}};

  // Retain complete argument arrays, including the executable, for each action.
  toml::array targets;
  for (const auto &target : graph.targets) {
    toml::array units;
    for (const auto &[source, object, arguments] : target.units)
      units.push_back(toml::table{{"source", source.string()},
                                  {"object", object.string()},
                                  {"arguments", values(arguments)}});

    targets.push_back(toml::table{{"name", target.name},
                                  {"output", target.output.string()},
                                  {"link", values(target.link)},
                                  {"units", std::move(units)}});
  }

  data.insert("targets", std::move(targets));
  std::ostringstream text;
  text << data << '\n';
  write_if_changed(project.build / "configuration.toml", text.str());
}

/// Restore only current, owned, location-matching configuration and revalidate
/// it.
Graph load_configuration(const Options &options) {
  const auto root = fs::canonical(options.root);
  const auto directory = build_directory(options);
  validate_build_directory(root, directory, true);

  const auto path = directory / "configuration.toml";
  const auto configure_hint = "run clutch \"" + root.string() + "\"" +
                              (options.release ? " --release" : "");
  if (!fs::is_regular_file(path))
    throw std::runtime_error("project is not configured; " + configure_hint);

  // Reject stale formats/manifests before accepting any cached command vectors.
  const auto data = toml::parse_file(path.string());
  if (data["schema"].value_or(0) != configuration_schema_version)
    throw std::runtime_error("saved configuration version is unsupported; " +
                             configure_hint);

  if (read_string(data, "manifest") != manifest_signature(root))
    throw std::runtime_error("clutch.toml changed since configuration; " +
                             configure_hint);

  // Saved absolute paths intentionally bind a graph to one location and
  // profile.
  Graph graph;
  auto &project = graph.project;
  project.root = read_string(data, "root");
  project.build = read_string(data, "build");
  if (project.root != root || project.build != directory ||
      read_string(data, "profile") != (options.release ? "release" : "debug"))
    throw std::runtime_error(
        "saved configuration belongs to another location/profile; " +
        configure_hint);

  project.output = read_string(data, "output");
  project.name = read_string(data, "name");
  project.language = read_string(data, "language");
  project.c_standard = read_string(data, "c_std");
  project.cxx_standard = read_string(data, "cpp_std");
  project.optimize = read_string(data, "optimize");
  project.warnings = read_string(data, "warnings");
  project.sources = read_paths(data, "sources");
  project.includes = read_paths(data, "includes");
  project.defines = read_strings(data, "defines");
  project.generator = read_string(data, "generator");
  graph.backend = {project.generator, read_string(data, "build_tool")};
  graph.toolchain = {read_string(data, "cc"),
                     read_string(data, "cxx"),
                     data["msvc"].value_or(false)};

  // Rebuild application/test commands without discovering sources or tools
  // again.
  const auto &targets = read_nonempty_array(
      data.get("targets"),
      "saved configuration has no targets; " + configure_hint);

  for (const auto &value : targets) {
    const auto &table = read_table(value, "invalid saved target");

    Target target;
    target.name = read_string(table, "name");
    target.output = read_string(table, "output");
    target.link = read_strings(table, "link");
    const auto &units = read_nonempty_array(table.get("units"),
                                            "invalid saved target commands");
    if (target.link.empty())
      throw std::runtime_error("invalid saved target commands");

    for (const auto &unit : units) {
      const auto &record = read_table(unit, "invalid saved compilation unit");

      Unit resolved{read_string(record, "source"),
                    read_string(record, "object"),
                    read_strings(record, "arguments")};
      if (resolved.arguments.empty())
        throw std::runtime_error("saved compilation command is empty");

      target.units.push_back(std::move(resolved));
    }

    graph.targets.push_back(std::move(target));
  }

  // Loading persisted data must enforce the same graph invariants as
  // configuration.
  validate_build_graph(graph);
  return graph;
}
} // namespace clutch