/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Load workspace manifests and qualify local target identities.
 */
#include "workspace.hpp"

#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/targets/requirements.hpp"
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
  workspace.dependency_requests.insert(workspace.dependency_requests.end(),
                                       member.dependency_requests.begin(),
                                       member.dependency_requests.end());
  const auto prefix = member.name + "::";
  if (member.targets.empty()) {
    TargetSpec application;
    application.name = member.name;
    application.sources = member.sources;
    application.additional_files = member.additional_files;
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
      spec.includes = merge_includes(member.includes, spec.includes);
      spec.defines = merge_defines(member.defines, spec.defines);
      spec.pch_c = merge_includes(member.pch_c, spec.pch_c);
      spec.pch_cpp = merge_includes(member.pch_cpp, spec.pch_cpp);

      for (auto *const links : {&spec.links, &spec.public_links})
        for (auto &link : *links)
          if (link.find("::") == std::string::npos ||
              std::any_of(member.dependency_requests.begin(),
                          member.dependency_requests.end(),
                          [&](const dependencies::Request &request) {
                            return link.rfind(request.requirement.alias + "::",
                                              0) == 0;
                          }))
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
  if (const auto *const node = document.get("workspace")) {
    const auto *const table = node->as_table();
    if (!table)
      throw std::runtime_error("workspace must be a table");

    for (const auto &[key, value] : *table) {
      (void)value;
      if (key.str() != "members")
        throw std::runtime_error("unknown workspace key: " +
                                 std::string(key.str()));
    }

    const auto *const members = (*table)["members"].as_array();
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
            "workspace member must be a physical directory: " +
            path.u8string());

      // Inspect every component, including symlinks that resolve back inside.
      auto component = root;
      for (const auto &part : relative) {
        component /= part;
        if (fs::is_symlink(component) || fs::canonical(component) != component)
          throw std::runtime_error("redirected workspace member: " +
                                   component.u8string());
      }

      if (!seen.insert(path).second)
        throw std::runtime_error("duplicate workspace member: " +
                                 path.u8string());

      if (!fs::is_regular_file(path / layout::manifest) ||
          fs::is_symlink(path / layout::manifest))
        throw std::runtime_error(
            "workspace member requires a regular clutch.toml: " +
            path.u8string());

      result.push_back(path);
    }
  }

  return result;
}

Project load_project(const Options &options) {
  const auto root = fs::canonical(options.root);
  const auto manifest = root / layout::manifest;
  std::vector<ConfigurationInput> inputs{{manifest, input_signature(manifest)}};
  const auto document = toml::parse_file(manifest.u8string());
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

    if (const auto *const build = document["build"].as_table())
      for (const auto &[key, value] : *build) {
        (void)value;
        if (key.str() == "sources" || key.str() == "links")
          throw std::runtime_error("workspace sources and application links "
                                   "require a root [project]");
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
  workspace.runtime = settings.runtime;
  workspace.is_workspace = true;
  workspace.language = "mixed";
  // Root compile settings are shared defaults; paths were resolved against the
  // root manifest before members are loaded in their independent source roots.
  workspace.includes = settings.includes;
  workspace.defines = settings.defines;
  workspace.pch_c = settings.pch_c;
  workspace.pch_cpp = settings.pch_cpp;

  if (!virtual_root) {
    // The real root's [build] settings are already workspace defaults. Applying
    // them again as member-local settings would incorrectly override public
    // library requirements and differ from standalone target resolution.
    settings.includes.clear();
    settings.defines.clear();
    settings.pch_c.clear();
    settings.pch_cpp.clear();
    append_project(workspace, std::move(settings));
  }

  for (const auto &path : members) {
    const auto input = path / layout::manifest;
    inputs.push_back({input, input_signature(input)});
    const auto child = toml::parse_file(input.u8string());
    if (child.contains("workspace"))
      throw std::runtime_error("nested workspaces are not supported: " +
                               input.u8string());

    if (child.contains("profile") || child["build"]["generator"] ||
        child["build"]["runtime"])
      throw std::runtime_error(
          "workspace root owns profiles, generator and runtime; "
          "remove member overrides: " +
          input.u8string());

    auto member_options = options;
    member_options.root = path;
    append_project(
        workspace,
        load_project_manifest(member_options, child, false, workspace.build));
  }

  workspace.inputs = std::move(inputs);
  validate_inputs(workspace.inputs);
  return workspace;
}
}  // namespace clutch
