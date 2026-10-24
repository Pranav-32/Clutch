/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Run optional developer tools using argv vectors and owned project
 * files.
 */
#include "tooling.hpp"

#include "core/configuration_store.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/source_files.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace clutch {
namespace {
/// Explicit paths never fall back to an unrelated installed tool.
std::string resolve_tool(const std::string &requested,
                         const std::vector<std::string> &defaults) {
  for (const auto &name :
       requested.empty() ? defaults : std::vector{requested}) {
    if (const auto found = find_program(name); !found.empty())
      return found;
  }
  throw std::runtime_error("developer tool not found; install " +
                           defaults.front() + " or pass --tool <executable>");
}

/// Only manifest projects are owned; dependency targets are excluded by root.
bool owns(const Project &project, const fs::path &root) {
  if (!project.is_workspace)
    return root == project.root;

  return std::any_of(project.projects.begin(),
                     project.projects.end(),
                     [&](const WorkspaceProject &member) {
                       return member.root == root;
                     });
}
}  // namespace

int format_project(const Options &options,
                   const std::string &tool,
                   const bool check) {
  const auto project = load_project(options);
  const auto formatter = resolve_tool(tool, {"clang-format"});
  std::set<fs::path> files;
  const auto collect = [&](const auto &sources, const auto &headers) {
    files.insert(sources.begin(), sources.end());
    files.insert(headers.begin(), headers.end());
  };
  collect(project.sources, project.additional_files);
  for (const auto *const group : {&project.targets, &project.tests})
    for (const auto &target : *group)
      collect(target.sources, target.additional_files);

  // Add sibling headers even when a manifest lists only translation units.
  // Do not scan unrelated repository folders or vendored include directories.
  std::set<fs::path> directories;
  for (const auto &file : files)
    directories.insert(file.parent_path());

  for (const auto &directory : directories) {
    const auto headers =
        expand_sources(directory, {"*.{h,H,hpp,hh,hxx}"}, project.build, false);
    files.insert(headers.additional_files.begin(),
                 headers.additional_files.end());
  }

  // Noncompiled IDE records can include text resources rather than C/C++.
  for (auto it = files.begin(); it != files.end();) {
    const auto extension = it->extension().u8string();
    if (std::find(source_extensions.begin(),
                  source_extensions.end(),
                  extension) == source_extensions.end() &&
        std::find(header_extensions.begin(),
                  header_extensions.end(),
                  extension) == header_extensions.end())
      it = files.erase(it);
    else
      ++it;
  }

  // One file per invocation bounds command length on every platform and keeps
  // diagnostics attributable. Files in dependencies/generated trees stay out.
  for (const auto &file : files) {
    status(check ? "Checking" : "Formatting", file.u8string());
    std::vector command{formatter};
    if (check)
      command.insert(command.end(), {"--dry-run", "--Werror"});
    else
      command.emplace_back("-i");

    command.push_back(file.u8string());
    if (const auto code = run(command, project.root); code != 0)
      return code;
  }
  status("Finished", std::to_string(files.size()) + " source/header files");
  return 0;
}

int lint_project(const Options &options,
                 const std::string &tool,
                 const std::string &checks) {
  const auto graph = load_configuration(options);
  const auto analyzer = resolve_tool(tool, {"clang-tidy"});
  std::set<fs::path> sources;
  for (const auto &target : graph.targets)
    if (owns(graph.project, target.source_root))
      for (const auto &unit : target.units)
        sources.insert(unit.source);

  for (const auto &source : sources) {
    status("Analyzing", source.u8string());
    std::vector<std::string> command{analyzer,
                                     "-p",
                                     graph.project.build.u8string()};
    if (!checks.empty())
      command.push_back("--checks=" + checks);

    if (graph.project.safety.strictness == "strict")
      command.emplace_back("--warnings-as-errors=*");

    command.push_back(source.u8string());
    if (const auto code = run(command, graph.project.root); code != 0)
      return code;
  }
  status("Finished", std::to_string(sources.size()) + " translation units");
  return 0;
}

int debug_target(const Graph &graph,
                 const size_t index,
                 const std::string &tool,
                 const std::vector<std::string> &arguments) {
  if (!graph.project.debug_symbols)
    throw std::runtime_error("debug requires profile.debug_symbols = true; "
                             "reconfigure the selected profile");

  const auto &target = graph.targets.at(index);
  if (target.type != TargetType::Executable)
    throw std::runtime_error("debug requires an executable target");

  const auto debugger = resolve_tool(tool, {"lldb", "gdb", "cdb"});
  auto name = fs::u8path(debugger).stem().u8string();
  std::transform(name.begin(),
                 name.end(),
                 name.begin(),
                 [](const unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  std::vector command{debugger};
  if (name == "lldb" || name.rfind("lldb-", 0) == 0)
    command.emplace_back("--");
  else if (name == "gdb" || name.rfind("gdb-", 0) == 0)
    command.emplace_back("--args");
  else if (name != "cdb")
    throw std::runtime_error(
        "debug supports lldb, gdb and cdb executable names");

  command.push_back(target.output.u8string());
  command.insert(command.end(), arguments.begin(), arguments.end());
  status("Debugging", target.name);
  return run(command, target.source_root);
}
}  // namespace clutch
