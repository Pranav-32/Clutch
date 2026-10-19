/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Validate clutch.toml and resolve project sources, standards, profiles
 * and tests.
 */

#include "project.hpp"

#include "core/build_directory.hpp"
#include "core/dependencies/manifest.hpp"
#include "core/options.hpp"
#include "core/pch/headers.hpp"
#include "core/project_layout.hpp"
#include "core/source_files.hpp"
#include "core/targets/manifest.hpp"
#include "core/workspace/workspace.hpp"
#include <toml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clutch {
namespace {
// Manifest-supported language standards. Array deduction
// preserves these immutable string literals without allocating temporary sets.
constexpr std::array cpp_standards{"11", "14", "17", "20", "23"};
constexpr std::array c_standards{"99", "11", "17", "23"};

/// Borrow a required table from the parsed document, which must outlive the
/// returned reference; report invalid node types with the supplied context.
const toml::table &require_table(const toml::node *const node,
                                 const char *const error) {
  if (const auto *const table = node ? node->as_table() : nullptr; table)
    return *table;

  throw std::runtime_error(error);
}

/// Borrow an array owned by the document, which must outlive the returned
/// reference. The diagnostic text is never retained.
const toml::array &require_array(const toml::node *const node,
                                 const std::string_view error) {
  if (const auto *const array = node ? node->as_array() : nullptr; array)
    return *array;

  throw std::runtime_error(std::string(error));
}

/// Copy a string value so callers do not retain references into a TOML node.
std::string require_string(const toml::node &node, const std::string &error) {
  if (const auto value = node.value<std::string>(); value)
    return *value;

  throw std::runtime_error(error);
}

/// Accept existing regular files with one of Clutch's C/C++ source suffixes.
bool supported_source(const fs::path &path) {
  return fs::is_regular_file(path) &&
         std::find(source_extensions.begin(),
                   source_extensions.end(),
                   path.extension().u8string()) != source_extensions.end();
}

/// Reject unknown keys explicitly so misspelled settings never fall back
/// silently.
void keys(const toml::table &table,
          const std::set<std::string> &allowed,
          const std::string &context) {
  for (const auto &[key, node] : table) {
    (void)node;
    if (!allowed.count(std::string(key.str())))
      throw std::runtime_error("unknown " + context +
                               " key: " + std::string(key.str()));
  }
}

/// Normalize an existing integer/string standard node to its compiler-facing
/// text.
std::string standard(const toml::node *const node, const std::string &context) {
  if (const auto value = node->value<std::string>())
    return *value;

  if (const auto *const value = node->as_integer(); value)
    return std::to_string(value->get());

  throw std::runtime_error(context +
                           " must be an integer or string standard version");
}
}  // namespace

/// Resolve manifest values into absolute paths and validated project settings.
Project load_project_manifest(const Options &options,
                              const toml::table &doc,
                              const bool virtual_workspace,
                              const fs::path &shared_build) {
  // Establish one physical project root for source, profile and ownership
  // checks.
  Project p;
  p.root = fs::canonical(options.root);
  p.build = shared_build.empty() ? build_directory(options) : shared_build;
  if (doc.contains("package"))
    throw std::runtime_error("rename [package] to [project] in clutch.toml");

  // Validate supported tables and types before applying any defaults.
  keys(doc,
       {"project",
        "build",
        "profile",
        "dependencies",
        "test",
        "target",
        "workspace"},
       "manifest");
  const toml::table empty_project;
  const auto &project =
      virtual_workspace
          ? empty_project
          : require_table(doc.get("project"), "clutch.toml: missing [project]");

  if (project.contains("edition") || project.contains("c_standard"))
    throw std::runtime_error("replace project.edition/c_standard with "
                             "project.language and project.std");

  keys(project, {"name", "version", "language", "std", "entry"}, "project");
  for (const auto &[key, node] : project) {
    if (key.str() != "std" && !node.is_string())
      throw std::runtime_error("project." + std::string(key.str()) +
                               " must be a string");
  }

  if (const auto *const node = doc.get("build")) {
    const auto &build = require_table(node, "build must be a table");

    if (build.contains("toolchain"))
      throw std::runtime_error(
          "build.toolchain was renamed to build.generator; select the build "
          "generator with generator = \"ninja\" under [build]");

    keys(build,
         {"sources",
          "include_dirs",
          "defines",
          "generator",
          "links",
          "pch",
          "runtime"},
         "build");
    if (const auto *const value = build.get("generator");
        value && !value->is_string())
      throw std::runtime_error("build.generator must be a string");
    if (const auto *const value = build.get("runtime");
        value && !value->is_string())
      throw std::runtime_error("build.runtime must be a string");
  }

  if (const auto *const node = doc.get("profile")) {
    const auto &profiles = require_table(node, "profile must be a table");

    keys(profiles, {"debug", "release"}, "profile");
    for (const auto &[name, value] : profiles) {
      const auto &profile =
          require_table(&value, "profile must contain tables");

      keys(profile,
           {"optimize", "warnings", "build_dir"},
           "profile." + std::string(name.str()));
      for (const auto &[key, setting] : profile) {
        if (!setting.is_string())
          throw std::runtime_error("profile." + std::string(name.str()) + "." +
                                   std::string(key.str()) +
                                   " must be a string");
      }
    }
  }

  p.name = virtual_workspace ? "workspace"
                             : doc["project"]["name"].value_or(std::string{});
  if (p.name.empty() ||
      !std::all_of(p.name.begin(), p.name.end(), [](const unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
      }))
    throw std::runtime_error(
        "project.name must contain only letters, digits, '_' or '-'");

  p.version = project["version"].value_or(std::string{});
  if (!virtual_workspace && p.version.empty())
    throw std::runtime_error("project.version must be a nonempty string");

  if (virtual_workspace && doc.contains("dependencies"))
    throw std::runtime_error("dependency imports require a [project] or belong "
                             "in workspace members");
  for (auto &requirement : dependencies::read_requirements(doc, p.root))
    p.dependency_requests.push_back({p.name, std::move(requirement)});
  const auto precompiled = pch::read(doc["build"]["pch"].node(), "build.pch");
  p.pch_c = pch::paths(precompiled.c, p.root);
  p.pch_cpp = pch::paths(precompiled.cpp, p.root);

  // Apply one checked array-of-strings reader to sources, includes and defines.
  const auto array = [&](const char *const key, const auto &append) {
    if (const auto node = doc["build"][key]; node) {
      const auto &values = require_array(node.node(),
                                         std::string("build.") + key +
                                             " must be an array of strings");
      for (const auto &value : values)
        append(require_string(value,
                              std::string("build.") + key +
                                  " must contain strings"));
    }
  };

  const bool application_sources =
      doc["project"]["entry"] || doc["build"]["sources"];
  std::set<fs::path> discovered_sources;
  fs::path explicit_entry;
  if (!virtual_workspace && (!doc.contains("target") || application_sources ||
                             doc["build"]["links"])) {
    // Prefer explicit sources; otherwise discover src/ and add any explicit
    // entry.
    std::vector<std::string> patterns;
    array("sources", [&](const std::string &s) {
      patterns.push_back(s);
    });
    auto selected = expand_sources(p.root, patterns, p.build);
    p.sources = std::move(selected.sources);
    p.additional_files = std::move(selected.additional_files);
    if (patterns.empty() && fs::is_directory(p.root / "src")) {
      // Automatic discovery shares generated-tree exclusions with explicit
      // globs. An empty src/ is permitted when project.entry supplies the
      // program.
      if (!fs::is_empty(p.root / "src")) {
        auto discovered = expand_sources(p.root, {"src/*"}, p.build, false);
        discovered_sources.insert(discovered.sources.begin(),
                                  discovered.sources.end());
        p.additional_files = std::move(discovered.additional_files);
      }
      p.sources.insert(p.sources.end(),
                       discovered_sources.begin(),
                       discovered_sources.end());
    }

    if (const auto entry = doc["project"]["entry"].value<std::string>()) {
      explicit_entry = fs::canonical(p.root / fs::u8path(*entry));
      p.sources.push_back(explicit_entry);
    }
  }

  if (!virtual_workspace) {
    // Normalize before target parsing: a matching application may omit sources
    // when the project already supplies them through entry or discovery.
    std::sort(p.sources.begin(), p.sources.end());
    p.sources.erase(std::unique(p.sources.begin(), p.sources.end()),
                    p.sources.end());
    load_manifest_targets(doc, p);

    // Explicit targets own their discovered files. Deliberately listed build
    // sources and the entry remain in the application even if another target
    // also compiles them; only automatic discovery yields ownership.
    p.sources.erase(
        std::remove_if(p.sources.begin(),
                       p.sources.end(),
                       [&](const fs::path &source) {
                         return source != explicit_entry &&
                                discovered_sources.count(source) &&
                                std::any_of(p.targets.begin(),
                                            p.targets.end(),
                                            [&](const TargetSpec &target) {
                                              return target.name != p.name &&
                                                     std::find(
                                                         target.sources.begin(),
                                                         target.sources.end(),
                                                         source) !=
                                                         target.sources.end();
                                            });
                       }),
        p.sources.end());
    complete_application_target(doc, p);
  }

  if (!p.targets.empty()) {
    p.sources.clear();
    for (const auto &target : p.targets)
      p.sources.insert(p.sources.end(),
                       target.sources.begin(),
                       target.sources.end());
  }

  // Stable, unique ordering also stabilizes object names across configurations.
  std::sort(p.sources.begin(), p.sources.end());
  p.sources.erase(std::unique(p.sources.begin(), p.sources.end()),
                  p.sources.end());
  if (!virtual_workspace && p.sources.empty())
    throw std::runtime_error(
        "no sources: add src/main.cpp, src/main.c or build.sources");

  for (const auto &source : p.sources) {
    if (!supported_source(source))
      throw std::runtime_error("unsupported source file: " + source.u8string());
  }

  // Infer the application's languages independently from declared test sources.
  p.language = doc["project"]["language"].value_or(std::string("auto"));
  if (p.language == "c++")
    p.language = "cpp";

  const bool has_c = std::any_of(p.sources.begin(),
                                 p.sources.end(),
                                 [](const fs::path &source) {
                                   return source.extension() == ".c";
                                 });
  const bool has_cpp = std::any_of(p.sources.begin(),
                                   p.sources.end(),
                                   [](const fs::path &source) {
                                     return source.extension() != ".c";
                                   });
  if (p.language == "auto")
    p.language = has_c && has_cpp ? "mixed" : has_c ? "c" : "cpp";

  if (p.language != "c" && p.language != "cpp" && p.language != "mixed")
    throw std::runtime_error(
        "project.language must be c, cpp (or c++), mixed or auto");

  if ((p.language == "c" && has_cpp) || (p.language == "cpp" && has_c))
    throw std::runtime_error("project.language does not match application "
                             "sources; use mixed for C and C++");

  // Mixed projects need separate standards; single-language projects accept a
  // scalar. Unspecified values retain the C11/C++17 defaults in Project.
  if (const auto *const node = project.get("std")) {
    if (const auto *const standards = node->as_table()) {
      keys(*standards, {"c", "cpp"}, "project.std");
      if (const auto *const value = standards->get("c"))
        p.c_standard = standard(value, "project.std.c");

      if (const auto *const value = standards->get("cpp"))
        p.cxx_standard = standard(value, "project.std.cpp");
    } else {
      if (p.language == "mixed")
        throw std::runtime_error("mixed-language project.std must be a table, "
                                 "such as { c = 11, cpp = 20 }");
      (p.language == "c" ? p.c_standard : p.cxx_standard) =
          standard(node, "project.std");
    }
  }

  if (std::find(cpp_standards.begin(), cpp_standards.end(), p.cxx_standard) ==
      cpp_standards.end())
    throw std::runtime_error("unsupported C++ std: " + p.cxx_standard);

  if (std::find(c_standards.begin(), c_standards.end(), p.c_standard) ==
      c_standards.end())
    throw std::runtime_error("unsupported C std: " + p.c_standard);

  // CLI generator selection takes precedence over the manifest's default.
  p.generator = options.generator.empty()
                    ? doc["build"]["generator"].value_or(std::string("ninja"))
                    : options.generator;
  p.runtime = doc["build"]["runtime"].value_or(std::string("static"));
  if (p.runtime != "static" && p.runtime != "dynamic")
    throw std::runtime_error("build.runtime must be static or dynamic");
  std::transform(p.generator.begin(),
                 p.generator.end(),
                 p.generator.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (p.generator.empty())
    throw std::runtime_error("build.generator must not be empty");

  // Resolve shared search paths and definitions once for application/test
  // units.
  array("include_dirs", [&](const std::string &s) {
    if (const auto path = fs::canonical(p.root / fs::u8path(s));
        fs::is_directory(path))
      p.includes.push_back(path);
    else
      throw std::runtime_error("include directory is not a folder: " + s);
  });
  if (fs::is_directory(p.root / "include"))
    p.includes.push_back(fs::canonical(p.root / "include"));

  array("defines", [&](const std::string &s) {
    p.defines.push_back(s);
  });

  // Apply only the selected profile and keep its artifacts in its own
  // directory.
  const auto profile = doc["profile"][options.release ? "release" : "debug"];
  p.optimize = profile["optimize"].value_or(
      std::string(options.release ? "speed" : "none"));
  p.warnings = profile["warnings"].value_or(std::string("extra"));
  if (p.optimize != "speed" && p.optimize != "size" && p.optimize != "none")
    throw std::runtime_error("optimize must be speed, size or none");

  if (p.warnings != "extra" && p.warnings != "default" && p.warnings != "none")
    throw std::runtime_error("warnings must be extra, default or none");

  p.output = p.build / p.name;
#ifdef _WIN32
  p.output += ".exe";
#endif
  return p;
}
}  // namespace clutch
