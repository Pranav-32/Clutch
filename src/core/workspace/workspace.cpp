/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Load workspace manifests and qualify local target identities.
 */
#include "workspace.hpp"

#include "core/options.hpp"
#include "core/project_layout.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace clutch {
namespace {
/// Scope member-wide compile requirements to its targets, not its neighbors.
void append_project(Project &workspace, Project member) {
  if (std::any_of(workspace.projects.begin(),
                  workspace.projects.end(),
                  [&](const WorkspaceProject &project) {
                    return project.name == member.name;
                  }))
    throw std::runtime_error("duplicate workspace project name: " +
                             member.name);

  workspace.projects.push_back({member.name, member.version, member.root});
  const auto prefix = member.name + "::";
  if (member.targets.empty()) {
    TargetSpec application;
    application.name = member.name;
    application.sources = member.sources;
    member.targets.push_back(std::move(application));
  }

  const auto append = [&](std::vector<TargetSpec> &specs,
                          std::vector<TargetSpec> &destination) {
    for (auto &spec : specs) {
      spec.local_name = spec.name;
      spec.project_name = member.name;
      spec.source_root = member.root;
      spec.c_standard = member.c_standard;
      spec.cxx_standard = member.cxx_standard;
      spec.name = prefix + spec.local_name;
      spec.includes.insert(spec.includes.begin(),
                           member.includes.begin(),
                           member.includes.end());
      spec.defines.insert(spec.defines.begin(),
                          member.defines.begin(),
                          member.defines.end());

      for (auto *links : {&spec.links, &spec.public_links})
        for (auto &link : *links)
          if (link.find("::") == std::string::npos)
            link.insert(0, prefix);

      workspace.sources.insert(workspace.sources.end(),
                               spec.sources.begin(),
                               spec.sources.end());
      destination.push_back(std::move(spec));
    }
  };

  append(member.targets, workspace.targets);
  append(member.tests, workspace.tests);
}
}  // namespace

std::vector<fs::path> workspace_member_roots(const fs::path &root,
                                             const toml::table &document) {
  std::vector<fs::path> result;
  if (const auto *node = document.get("workspace")) {
    const auto *table = node->as_table();
    if (!table)
      throw std::runtime_error("workspace must be a table");

    for (const auto &[key, value] : *table) {
      (void)value;
      if (key.str() != "members")
        throw std::runtime_error("unknown workspace key: " +
                                 std::string(key.str()));
    }

    const auto *members = (*table)["members"].as_array();
    if (!members || members->empty())
      throw std::runtime_error(
          "workspace.members must be a nonempty array of directory paths");

    std::set<fs::path> seen;
    for (const auto &value : *members) {
      const auto text = value.value<std::string>();
      if (!text || text->empty())
        throw std::runtime_error(
            "workspace.members must contain nonempty strings");

      const auto spelling = fs::u8path(*text);
      if (spelling.is_absolute())
        throw std::runtime_error("workspace member paths must be relative");

      const auto path = fs::absolute(root / spelling).lexically_normal();
      const auto relative = path.lexically_relative(root);
      if (relative.empty() || relative == "." || *relative.begin() == "..")
        throw std::runtime_error(
            "workspace members must be inside the workspace root");

      if (!fs::is_directory(path) || fs::canonical(path) != path)
        throw std::runtime_error(
            "workspace member must be a physical directory: " + path.string());

      // Inspect every component, including symlinks that resolve back inside.
      auto component = root;
      for (const auto &part : relative) {
        component /= part;
        if (fs::is_symlink(component) || fs::canonical(component) != component)
          throw std::runtime_error("redirected workspace member: " +
                                   component.string());
      }

      if (!seen.insert(path).second)
        throw std::runtime_error("duplicate workspace member: " +
                                 path.string());

      if (!fs::is_regular_file(path / layout::manifest) ||
          fs::is_symlink(path / layout::manifest))
        throw std::runtime_error(
            "workspace member requires a regular clutch.toml: " +
            path.string());

      result.push_back(path);
    }
  }

  return result;
}

Project load_project(const Options &options) {
  const auto root = fs::canonical(options.root);
  const auto manifest = root / layout::manifest;
  std::vector<ConfigurationInput> inputs{{manifest, input_signature(manifest)}};
  const auto document = toml::parse_file(manifest.string());
  if (!document.contains("workspace")) {
    auto project = load_project_manifest(options, document);
    project.projects.push_back({project.name, project.version, project.root});
    project.inputs = std::move(inputs);
    validate_inputs(project.inputs);
    return project;
  }

  const auto members = workspace_member_roots(root, document);
  const bool virtual_root = !document.contains("project");
  if (virtual_root) {
    if (document.contains("target") || document.contains("test"))
      throw std::runtime_error(
          "virtual workspace targets belong in a member or a root [project]");

    if (const auto *build = document["build"].as_table())
      for (const auto &[key, value] : *build) {
        (void)value;
        if (key.str() != "generator")
          throw std::runtime_error(
              "virtual workspace build settings support only generator");
      }
  }

  auto settings = load_project_manifest(options, document, virtual_root);
  Project workspace;
  workspace.root = root;
  workspace.build = settings.build;
  workspace.name = settings.name;
  workspace.generator = settings.generator;
  workspace.optimize = settings.optimize;
  workspace.warnings = settings.warnings;
  workspace.is_workspace = true;
  workspace.language = "mixed";

  if (!virtual_root)
    append_project(workspace, std::move(settings));

  for (const auto &path : members) {
    const auto input = path / layout::manifest;
    inputs.push_back({input, input_signature(input)});
    const auto child = toml::parse_file(input.string());
    if (child.contains("workspace"))
      throw std::runtime_error("nested workspaces are not supported: " +
                               input.string());

    if (child.contains("profile") || child["build"]["generator"])
      throw std::runtime_error("workspace root owns profiles and generator; "
                               "remove member overrides: " +
                               input.string());

    auto member_options = options;
    member_options.root = path;
    append_project(workspace, load_project_manifest(member_options, child));
  }

  workspace.inputs = std::move(inputs);
  validate_inputs(workspace.inputs);
  return workspace;
}
}  // namespace clutch
