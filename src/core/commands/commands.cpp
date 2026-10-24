/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Launch explicit project commands without implicit shell evaluation.
 */
#include "commands.hpp"

#include "core/build_directory.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "manifest.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include <algorithm>
#include <map>

namespace clutch {
namespace {
/// Substitute only named context tokens, once; @@ escapes a literal @.
std::string expand(const std::string &text,
                   const Project &project,
                   const Options &options) {
  const std::map<std::string, std::string> variables{
      {"PROJECT_DIR", project.root.u8string()},
      {"BUILD_DIR", project.build.u8string()},
      {"PROFILE", options.release ? "release" : "debug"}};
  std::string result;
  for (size_t index = 0; index < text.size();) {
    if (text[index] != '@') {
      result += text[index++];
      continue;
    }

    if (index + 1U < text.size() && text[index + 1U] == '@') {
      result += '@';
      index += 2U;
      continue;
    }

    if (const auto end = text.find('@', index + 1U); end != text.npos) {
      const auto key = text.substr(index + 1U, end - index - 1U);
      if (const auto found = variables.find(key); found != variables.end())
        result += found->second;
      else
        throw std::runtime_error("unknown command variable: " + key);

      index = end + 1U;
    } else
      result += text[index++];
  }

  return result;
}

/// Resolve explicit relative executables against the command's working dir.
/// Keep compiler/tool aliases intact rather than canonicalizing symlink names.
int execute(const ProjectCommand &command,
            const Project &project,
            const Options &options,
            const std::vector<std::string> &arguments) {
  const auto directory =
      fs::absolute(project.root /
                   fs::u8path(expand(command.working_dir, project, options)))
          .lexically_normal();
  if (!fs::is_directory(directory))
    throw std::runtime_error("command working directory does not exist: " +
                             directory.u8string());

  std::vector<std::string> argv;
  argv.reserve(command.argv.size() + arguments.size());
  for (const auto &argument : command.argv)
    argv.push_back(expand(argument, project, options));

  const auto executable = fs::u8path(argv.front());
  if (executable.has_parent_path() && !executable.is_absolute())
    argv.front() = (directory / executable).lexically_normal().u8string();

  if (const auto found = find_program(argv.front()); !found.empty())
    argv.front() = found;
  else
    throw std::runtime_error("command executable not found: " + argv.front());

  argv.insert(argv.end(), arguments.begin(), arguments.end());
  status("Running", command.name);
  return run(argv, directory);
}
}  // namespace

void run_command_hooks(const Project &project,
                       const Options &options,
                       const std::string &phase) {
  for (const auto &command : project.commands)
    if (std::find(command.phases.begin(), command.phases.end(), phase) !=
        command.phases.end())
      if (const auto code = execute(command, project, options, {}); code != 0)
        throw CommandFailure(command.name, code);
}

int execute_project_command(const Options &options,
                            const std::string &name,
                            const std::vector<std::string> &arguments) {
  Project project;
  project.root = fs::canonical(options.root);
  project.build = build_directory(options);
  const auto document =
      toml::parse_file((project.root / layout::manifest).u8string());
  for (const auto &command : read_project_commands(document))
    if (command.name == name)
      return execute(command, project, options, arguments);

  throw std::runtime_error("unknown project command: " + name);
}
}  // namespace clutch
