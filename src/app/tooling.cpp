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
#include "platform/capture.hpp"
#include "platform/environment.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include "platform/tool_discovery.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace clutch {
namespace {
/// Probe automatic debugger candidates without loading user initialization
/// files. Interactive-session failures are never retried with a different
/// debugger.
bool debugger_starts(const std::string &executable, const fs::path &directory) {
  const auto name = fs::u8path(executable).stem().u8string();
  std::vector command{executable};
  if (name == "lldb" || name.rfind("lldb-", 0) == 0)
    command.insert(command.end(), {"--no-lldbinit", "--batch", "-o", "quit"});
  else if (name == "gdb" || name.rfind("gdb-", 0) == 0)
    command.insert(command.end(), {"--nx", "--batch", "--version"});
  else
    // No verified noninteractive startup form (for example cdb): accept the
    // discovered executable rather than skip it on an unproven probe.
    return true;

  // Optional tools must not hang discovery indefinitely during startup.
  constexpr auto debugger_startup_timeout = std::chrono::seconds{10};
  const auto result =
      capture(command, directory, true, debugger_startup_timeout);
  if (result.status == 0)
    return true;

  const auto end = result.text.find_first_of("\r\n");
  status("Skipping",
         executable + ": " +
             (result.text.empty() ? "debugger startup failed (exit " +
                                        std::to_string(result.status) + ")"
                                  : result.text.substr(0, end)));
  return false;
}

/// Explicit paths and environment choices never fall back to unrelated tools.
std::string resolve_tool(const std::string &requested,
                         const char *const variable,
                         const std::vector<std::string> &defaults,
                         const fs::path &directory,
                         const bool probe_debugger = false) {
  auto selection = requested;
  if (const auto value = environment_value(variable);
      selection.empty() && value)
    selection = *value;

  if (!selection.empty()) {
    if (const auto found = find_tool(selection); !found.empty())
      return found;

    throw std::runtime_error("developer tool not found: " + selection +
                             "; check " + variable + " or --tool");
  }

  // Check all PATH tool families before falling back to any IDE installation.
  std::vector<std::string> candidates;
  for (const auto &name : defaults)
    if (const auto found = find_program(name); !found.empty())
      candidates.push_back(found);

  for (const auto &name : defaults)
    for (const auto &found : tool_candidates(name))
      if (std::find(candidates.begin(), candidates.end(), found) ==
          candidates.end())
        candidates.push_back(found);

  for (const auto &candidate : candidates) {
    try {
      if (!probe_debugger || debugger_starts(candidate, directory))
        return candidate;
    } catch (const std::runtime_error &error) {
      status("Skipping", candidate + ": " + error.what());
    }
  }

  throw std::runtime_error("usable developer tool not found; install " +
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
  const auto formatter =
      resolve_tool(tool, "CLUTCH_FORMATTER", {"clang-format"}, project.root);
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
  const auto analyzer =
      resolve_tool(tool, "CLUTCH_ANALYZER", {"clang-tidy"}, graph.project.root);
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

  const auto debugger = resolve_tool(tool,
                                     "CLUTCH_DEBUGGER",
                                     {"lldb", "gdb", "cdb"},
                                     target.source_root,
                                     true);
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
