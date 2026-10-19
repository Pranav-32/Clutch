/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Relocate manifest paths and snapshot resolved source dependencies.
 */
#include "bundle.hpp"

#include "core/dependencies/resolve.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/zip.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <toml.hpp>

namespace clutch {
namespace {
constexpr auto snapshot_directory = ".clutch/bundle/deps";
constexpr auto metadata_file = ".clutch/bundle/snapshot.toml";
constexpr auto instructions_file = ".clutch/bundle/README.txt";
constexpr auto maximum_entries = size_t{65534};  // Classic ZIP member bound.
constexpr auto format_flags = toml::toml_formatter::default_flags &
                              ~toml::format_flags::allow_literal_strings &
                              ~toml::format_flags::allow_multi_line_strings;

/// Component comparisons keep similarly prefixed sibling trees distinct.
bool contains(const fs::path &parent, const fs::path &child) {
  return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end())
             .first == parent.end();
}

/// The first two length-prefixed lock fields identify an owner and local alias.
std::pair<std::string, std::string> requester(std::string_view text) {
  std::pair<std::string, std::string> result;
  for (auto *const value : {&result.first, &result.second}) {
    const auto colon = text.find(':');
    size_t size = 0;
    if (colon == text.npos || colon == 0)
      throw std::runtime_error("invalid bundle dependency request");
    const auto *const begin = text.data();
    const auto parsed = std::from_chars(begin, begin + colon, size);
    if (parsed.ec != std::errc{} || parsed.ptr != begin + colon ||
        size > text.size() - colon - 1)
      throw std::runtime_error("invalid bundle dependency request length");
    text.remove_prefix(colon + 1);
    *value = std::string(text.substr(0, size));
    text.remove_prefix(size);
  }
  return result;
}

/// One archive path per physical source root; nested local packages stay in
/// place.
class Snapshot {
  public:
  explicit Snapshot(const Project &project)
      : project_(project) {
    roots_.emplace(project.root, fs::path{});
    for (const auto &package : project.packages) {
      if (package.source.empty())
        continue;

      const auto relative = package.source.lexically_relative(project.root);
      const bool local =
          contains(project.root, package.source) &&
          !contains(project.root / layout::environment, package.source);
      roots_.emplace(package.source,
                     local ? relative
                           : fs::path(snapshot_directory) / package.id);
    }

    for (const auto &package : project.packages)
      for (const auto &request : package.requests)
        imports_.emplace(requester(request), package.source);
  }

  /// Map only declared source trees; never silently capture a machine's SDK.
  fs::path destination(const fs::path &source) const {
    const auto path = fs::weakly_canonical(source);
    const std::pair<const fs::path, fs::path> *best = nullptr;
    for (const auto &root : roots_)
      if (contains(root.first, path) &&
          (!best || root.first.native().size() > best->first.native().size()))
        best = &root;

    if (!best)
      throw std::runtime_error(
          "bundle path is outside the project and dependency source trees: " +
          source.u8string());
    const auto relative = path.lexically_relative(best->first);
    return (relative == "." ? best->second : best->second / relative)
        .lexically_normal();
  }

  /// Resolve manifest-relative paths before expressing them in archive space.
  std::string relocate(const std::string &text,
                       const fs::path &source,
                       const fs::path &target) const {
    if (text.find("{build}") != text.npos || text.find("{source}") != text.npos)
      return text;
    const auto mapped = destination(source / fs::u8path(text));
    const auto relative =
        mapped.lexically_relative(target.empty() ? fs::path(".") : target);
    return (relative.empty() ? fs::path(".") : relative).generic_u8string();
  }

  /// Rewrite known path arrays, retaining language tables for PCH declarations.
  void paths(toml::node *const node,
             const fs::path &source,
             const fs::path &target) const {
    if (!node)
      return;
    if (auto *const array = node->as_array()) {
      for (auto &item : *array) {
        if (auto *const text = item.as_string())
          text->get() = relocate(text->get(), source, target);
      }
    } else if (auto *const table = node->as_table()) {
      for (auto &[key, value] : *table) {
        (void)key;
        paths(&value, source, target);
      }
    }
  }

  /// Linker names stay names; explicit runtime/library files are path values.
  void library_paths(toml::node *const node,
                     const fs::path &source,
                     const fs::path &target,
                     const bool all) const {
    if (auto *const array = node ? node->as_array() : nullptr)
      for (auto &item : *array)
        if (auto *const text = item.as_string();
            text &&
            (all || text->get().find_first_of("/\\") != std::string::npos ||
             fs::u8path(text->get()).is_absolute()))
          text->get() = relocate(text->get(), source, target);
  }

  /// Transform only participating Clutch manifests, leaving foreign scripts
  /// intact.
  std::string manifest(const fs::path &source, const std::string &owner) const {
    auto document = toml::parse_file((source / layout::manifest).u8string());
    const auto target = destination(source);
    if (auto *const project = document["project"].as_table())
      if (auto *const entry = (*project)["entry"].as_string())
        entry->get() = relocate(entry->get(), source, target);

    const auto settings = [&](toml::table &table) {
      for (const auto *const key :
           {"sources", "include_dirs", "public_include_dirs", "pch"})
        paths(table.get(key), source, target);
    };
    if (auto *const build = document["build"].as_table())
      settings(*build);
    for (const auto *const group : {"target", "test"})
      if (auto *const array = document[group].as_array())
        for (auto &node : *array)
          if (auto *const table = node.as_table())
            settings(*table);

    // Output directories and rules are regenerated for the recipient's host.
    if (auto *const profiles = document["profile"].as_table())
      for (auto &[name, node] : *profiles)
        if (auto *const table = node.as_table())
          table->insert_or_assign(
              "build_dir",
              layout::default_build_directory(name.str() == "release"));

    if (auto *const dependencies = document["dependencies"].as_table())
      for (auto &[alias, node] : *dependencies) {
        auto *const table = node.as_table();
        if (!table)
          throw std::runtime_error("bundle dependency must be a table");
        auto dependency_source = source;
        auto dependency_target = target;
        if (!table->contains("system")) {
          const auto found = imports_.find({owner, std::string(alias.str())});
          if (found == imports_.end() || found->second.empty())
            throw std::runtime_error("bundle dependency is not resolved: " +
                                     std::string(alias.str()));
          dependency_source = found->second;
          dependency_target = destination(dependency_source);
          const auto relative = dependency_target.lexically_relative(
              target.empty() ? fs::path(".") : target);
          for (const auto *const key : {"git",
                                        "tag",
                                        "branch",
                                        "rev",
                                        "version",
                                        "vcpkg",
                                        "baseline",
                                        "triplet",
                                        "features",
                                        "root"})
            table->erase(key);
          table->insert_or_assign("path", relative.generic_u8string());
        } else if (auto *const root = (*table)["root"].as_string()) {
          dependency_source =
              fs::weakly_canonical(source / fs::u8path(root->get()));
          dependency_target = destination(dependency_source);
          root->get() = relocate(root->get(), source, target);
        }

        paths(table->get("include_dirs"), dependency_source, dependency_target);
        paths(table->get("pch"), dependency_source, dependency_target);
        library_paths(table->get("runtime"),
                      dependency_source,
                      dependency_target,
                      true);
        // Make output artifacts are relative to its fresh build tree.
        if ((*table)["build_system"].value_or(std::string{}) != "make")
          library_paths(table->get("libraries"),
                        dependency_source,
                        dependency_target,
                        false);
      }

    std::ostringstream text;
    text << "# Source snapshot generated by Clutch; dependency origins are in "
         << metadata_file << ".\n"
         << toml::toml_formatter(document, format_flags) << '\n';
    return text.str();
  }

  /// Gather all regular files and empty directories, with explicit exclusions.
  std::map<std::string, ZipEntry>
  collect(const fs::path &output,
          const std::vector<std::string> &excluded) const {
    std::vector<fs::path> exclusions;
    for (const auto &text : excluded) {
      const auto path = fs::u8path(text).lexically_normal();
      if (path.empty() || path == "." || path.is_absolute() ||
          *path.begin() == "..")
        throw std::runtime_error(
            "bundle exclusions must be relative archive paths");
      exclusions.push_back(path);
    }
    std::map<std::string, ZipEntry> entries;
    for (const auto &[source, target] : roots_)
      for (auto it = fs::recursive_directory_iterator(source);
           it != fs::recursive_directory_iterator();
           ++it) {
        const auto path = it->path();
        const auto name = path.filename().u8string();
        const auto relative = path.lexically_relative(source);
        const auto archive_path = (target / relative).lexically_normal();
        const bool omitted =
            path == output || name == ".git" || name == ".hg" ||
            name == ".svn" || name == layout::environment ||
            name == layout::lock_file || name == layout::lock_writer ||
            std::any_of(exclusions.begin(),
                        exclusions.end(),
                        [&](const fs::path &exclude) {
                          return contains(exclude, archive_path);
                        });
        if (omitted) {
          it.disable_recursion_pending();
          continue;
        }

        validate_generated_path(path);
        const auto status = it->symlink_status();
        const bool directory = fs::is_directory(status);
        if (directory && (path == project_.build ||
                          fs::exists(path / layout::owner_marker) ||
                          fs::exists(path / "CMakeCache.txt"))) {
          it.disable_recursion_pending();
          continue;
        }

        if (!directory && !fs::is_regular_file(status))
          throw std::runtime_error(
              "bundle rejects symlinks and special files: " + path.u8string());
        const auto key =
            archive_path.generic_u8string() + (directory ? "/" : "");
        entries.emplace(key, ZipEntry{key, directory ? fs::path{} : path, {}});
        if (entries.size() > maximum_entries)
          throw std::runtime_error(
              "bundle exceeds the classic ZIP member limit");
      }
    return entries;
  }

  private:
  const Project &project_;
  std::map<fs::path, fs::path> roots_;
  std::map<std::pair<std::string, std::string>, fs::path> imports_;
};
}  // namespace

fs::path bundle_project(const Options &options,
                        const fs::path &requested,
                        const std::vector<std::string> &exclusions) {
  auto project = load_project(options);
  auto resolution = options;
  resolution.offline = true;
  resolution.locked = true;
  dependencies::resolve(project, resolution);
  const auto output =
      fs::absolute(requested.empty()
                       ? project.root / layout::environment / "bundles" /
                             (project.name + "-sources.zip")
                       : requested)
          .lexically_normal();
  validate_generated_path(output);
  const Snapshot snapshot(project);
  auto entries = snapshot.collect(output, exclusions);
  std::map<fs::path, std::string> manifests;
  manifests.emplace(project.root, project.name);
  for (const auto &member : project.projects)
    if (fs::is_regular_file(member.root / layout::manifest))
      manifests.emplace(member.root, member.name);
  for (const auto &package : project.packages)
    if (!package.source.empty() &&
        fs::is_regular_file(package.source / layout::manifest))
      manifests.insert_or_assign(package.source, package.id);

  for (const auto &[root, owner] : manifests) {
    const auto name =
        (snapshot.destination(root) / layout::manifest).generic_u8string();
    const auto found = entries.find(name);
    if (found == entries.end())
      throw std::runtime_error("bundle excludes a required manifest: " + name);
    found->second.source.clear();
    found->second.content = snapshot.manifest(root, owner);
  }
  for (const auto &source : project.sources)
    if (!entries.count(snapshot.destination(source).generic_u8string()))
      throw std::runtime_error("bundle excludes a required source: " +
                               source.u8string());

  toml::array packages;
  for (const auto &package : project.packages) {
    toml::table record{{"id", package.id},
                       {"provider", package.provider},
                       {"version", package.version},
                       {"revision", package.revision},
                       {"reference", package.reference}};
    if (!package.source.empty())
      record.insert("path",
                    snapshot.destination(package.source).generic_u8string());
    if (package.git.find("://") != std::string::npos ||
        package.git.find('@') != std::string::npos)
      record.insert("git", package.git);
    packages.push_back(std::move(record));
  }
  std::ostringstream metadata;
  metadata << toml::toml_formatter(
                  toml::table{{"schema", 1},
                              {"project", project.name},
                              {"dependency", std::move(packages)}},
                  format_flags)
           << '\n';
  entries.emplace(metadata_file, ZipEntry{metadata_file, {}, metadata.str()});
  entries.emplace(
      instructions_file,
      ZipEntry{
          instructions_file,
          {},
          "Clutch source snapshot\n\nInstall Clutch, a C/C++ compiler and your "
          "chosen native build tool.\n"
          "From the extracted root: clutch . --offline; clutch build; clutch "
          "test\n"
          "Use --cc/--cxx/--generator when configuring for another host.\n"
          "Source dependencies are snapshots referenced by relative path "
          "imports.\n"
          "clutch.lock and native rules are regenerated for this location.\n"
          "Installed SDKs and tools remain prerequisites. Foreign scripts may "
          "need\n"
          "their own offline settings. Review included licenses before "
          "redistribution.\n"});
  std::vector<ZipEntry> files;
  for (auto &[name, entry] : entries) {
    (void)name;
    files.push_back(std::move(entry));
  }
  validate_inputs(project.inputs);
  write_zip(output, std::move(files));
  return output;
}
}  // namespace clutch
