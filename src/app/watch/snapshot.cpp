/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Bounded content polling for native source trees and explicit paths.
 */
#include "snapshot.hpp"

#include "core/build_directory.hpp"
#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/source_files.hpp"
#include "core/workspace/inputs.hpp"
#include "watch.hpp"
#include <toml.hpp>

#include <algorithm>
#include <stdexcept>

namespace clutch {
namespace {
// Bound traversal independently of compiler concurrency and debounce settings.
constexpr auto maximum_roots = 256U;
constexpr auto maximum_entries = 100000U;

/// Compare components so an ignored folder does not hide similarly named peers.
bool contains(const fs::path &parent, const fs::path &child) {
  return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end())
             .first == parent.end();
}

/// Native manifest, lock and upstream rule edits affect configuration metadata.
bool configuration_file(const fs::path &path) {
  const auto name = path.filename();
  return name == layout::manifest || name == layout::lock_file ||
         name == "CMakeLists.txt" || path.extension() == ".cmake" ||
         name == "Makefile" || name == "makefile" || name == "GNUmakefile";
}

/// Default traversal watches C/C++ inputs, not logs or arbitrary script output.
bool source_file(const fs::path &path) {
  const auto suffix = path.extension().u8string();
  return std::find(source_extensions.begin(),
                   source_extensions.end(),
                   suffix) != source_extensions.end() ||
         std::find(header_extensions.begin(),
                   header_extensions.end(),
                   suffix) != header_extensions.end();
}

/// A root collector can inspect declarations while source validation is
/// failing.
std::set<fs::path> source_roots(const fs::path &root,
                                const Graph *const graph) {
  std::set<fs::path> roots{root};
  if (graph) {
    for (const auto &project : graph->project.projects)
      roots.insert(project.root);

    for (const auto &package : graph->project.packages)
      if (package.provider == "path")
        roots.insert(package.source);
  }

  std::set<fs::path> inspected;
  while (inspected.size() < roots.size()) {
    if (roots.size() > maximum_roots)
      throw std::runtime_error("watch exceeds the source-root limit");

    const auto found =
        std::find_if(roots.begin(), roots.end(), [&](const auto &path) {
          return !inspected.count(path);
        });
    const auto &directory = *found;
    inspected.insert(directory);
    try {
      const auto doc =
          toml::parse_file((directory / layout::manifest).u8string());
      if (const auto *const members = doc["workspace"]["members"].as_array())
        for (const auto &member : *members)
          if (const auto path = member.value<std::string>();
              path && !path->empty())
            roots.insert(
                fs::absolute(directory / fs::u8path(*path)).lexically_normal());

      if (const auto *const dependencies = doc["dependencies"].as_table())
        for (const auto &[alias, node] : *dependencies) {
          (void)alias;
          if (const auto *const table = node.as_table())
            if (const auto path = (*table)["path"].value<std::string>();
                path && !path->empty())
              roots.insert(fs::absolute(directory / fs::u8path(*path))
                               .lexically_normal());
        }
    } catch (const std::exception &) {
      // A half-written/malformed manifest is handled by the configure cycle.
      // Its content remains watched so another edit can recover automatically.
      continue;
    }
  }

  return roots;
}
}  // namespace

void snapshot_watch_inputs(const Options &options,
                           const WatchSettings &settings,
                           const Graph *const graph,
                           WatchSnapshot &result) {
  result.files.clear();
  result.timestamps.clear();
  result.configuration.clear();
  const auto root = fs::canonical(options.root);
  std::set<fs::path> outputs;
  if (graph)
    outputs.insert(graph->project.build);

  // Exclude both declared profiles, including a fresh profile with no marker.
  for (const auto release : {false, true}) {
    auto profile = options;
    profile.release = release;
    try {
      outputs.insert(build_directory(profile));
    } catch (const std::exception &) {
      // Invalid settings are reported by configuration, not by the poller.
      outputs.insert(root / layout::default_build_directory(release));
    }
  }

  std::vector<fs::path> ignores;
  ignores.reserve(settings.ignores.size());
  for (const auto &path : settings.ignores)
    ignores.push_back(fs::absolute(root / fs::u8path(path)).lexically_normal());

  const auto excluded = [&](const fs::path &path) {
    for (const auto &component : path)
      if (component == ".git" || component == layout::environment)
        return true;

    for (const auto &output : outputs)
      if (contains(output, path))
        return true;

    return std::any_of(ignores.begin(), ignores.end(), [&](const auto &ignore) {
      return contains(ignore, path);
    });
  };

  const auto append = [&](const fs::path &path, const bool configuration) {
    if (excluded(path))
      return;

    if (configuration)
      result.configuration.insert(path);

    if (result.files.count(path))
      return;

    try {
      if (!fs::exists(path)) {
        result.files[path] = "missing";
        return;
      }

      if (fs::is_symlink(fs::symlink_status(path)))
        return;

      result.files[path] =
          fs::is_regular_file(path) ? input_signature(path) : "missing";
      if (fs::is_regular_file(path))
        result.timestamps[path] = fs::last_write_time(path);
    } catch (const std::exception &) {
      result.files[path] = "unreadable";
      result.timestamps.erase(path);
    }
  };

  size_t scanned = 0;
  const auto scan = [&](const fs::path &directory, const bool all_files) {
    if (excluded(directory))
      return;

    std::error_code error;
    if (!fs::is_directory(directory, error) ||
        fs::canonical(directory, error) != directory || error) {
      append(directory, true);
      return;
    }

    result.files.emplace(directory, "directory");
    for (auto it = fs::recursive_directory_iterator(
             directory,
             fs::directory_options::skip_permission_denied,
             error);
         !error && it != fs::recursive_directory_iterator();
         it.increment(error)) {
      if (++scanned > maximum_entries)
        throw std::runtime_error("watch exceeds the directory-entry limit");

      const auto path = it->path();
      const auto state = it->symlink_status(error);
      if (error)
        break;

      if (excluded(path) || fs::is_symlink(state)) {
        it.disable_recursion_pending();
        continue;
      }

      if (it->is_directory(error)) {
        if (fs::canonical(path, error) != path ||
            fs::exists(path / layout::owner_marker, error) ||
            fs::exists(path / "CMakeCache.txt", error) ||
            (!all_files && fs::exists(path / layout::manifest, error)))
          it.disable_recursion_pending();
      } else if (fs::is_regular_file(state) &&
                 (all_files || source_file(path) || configuration_file(path)))
        append(path, configuration_file(path));
    }
  };

  for (const auto &directory : source_roots(root, graph))
    scan(directory, false);

  // Always retain missing root inputs, so deletion and recreation are visible.
  append(root / layout::manifest, true);
  append(root / layout::lock_file, true);
  if (graph) {
    for (const auto &input : graph->project.inputs)
      append(input.path, true);

    for (const auto &target : graph->targets) {
      for (const auto &unit : target.units)
        append(unit.source, false);

      for (const auto &file : target.additional_files)
        append(file, false);
    }
  }

  for (const auto &text : settings.paths) {
    const auto path = fs::absolute(root / fs::u8path(text)).lexically_normal();
    if (fs::is_directory(path))
      scan(path, true);
    else
      append(path, configuration_file(path));
  }
}

bool watch_needs_configuration(const WatchSnapshot &before,
                               const WatchSnapshot &after) {
  if (before.files.size() != after.files.size())
    return true;

  auto previous = before.files.begin();
  for (const auto &[path, signature] : after.files) {
    if (previous->first != path)
      return true;

    if ((previous->second == "missing") != (signature == "missing"))
      return true;

    if (previous->second != signature &&
        (before.configuration.count(path) || after.configuration.count(path)))
      return true;

    ++previous;
  }

  return false;
}

bool watch_needs_clean(const WatchSnapshot &before,
                       const WatchSnapshot &after) {
  for (const auto &[path, timestamp] : after.timestamps) {
    const auto old = before.files.find(path);
    if (old == before.files.end() || old->second == after.files.at(path))
      continue;

    if (const auto previous = before.timestamps.find(path);
        previous == before.timestamps.end() || timestamp <= previous->second)
      return true;
  }

  return false;
}
}  // namespace clutch
