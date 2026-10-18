/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Execute native archive tools with fresh outputs and bounded command
 * lines.
 */
#include "archive.hpp"

#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <stdexcept>

namespace clutch {
std::vector<std::string> archive_command(const Graph &graph,
                                         const Options &options,
                                         const fs::path &executable,
                                         const size_t index) {
  std::vector<std::string> command{executable.string(),
                                   "__archive",
                                   "--project",
                                   graph.project.root.string(),
                                   "--target",
                                   graph.targets.at(index).name};
  if (options.release)
    command.emplace_back("--release");
  return command;
}

int archive_target(const Graph &graph, const size_t index) {
  const auto &target = graph.targets.at(index);
  if (target.type != TargetType::StaticLibrary || target.link.empty())
    throw std::runtime_error(
        "internal archive action requires a static-library target");

  const auto relative = target.output.lexically_relative(graph.project.build);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
      fs::is_symlink(target.output))
    throw std::runtime_error(
        "refusing archive output outside the owned build directory");

  std::vector<std::string> command;
  const auto response = fs::path(target.output.string() + ".archive.rsp");
  if (fs::is_symlink(response))
    throw std::runtime_error("refusing redirected archive response file");

#ifdef __APPLE__
  if (fs::path(target.link.front()).filename() == "libtool") {
    std::string files;
    for (const auto &unit : target.units) {
      if (unit.object.string().find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error(
            "newlines are not supported in archive file lists");
      files += unit.object.string() + '\n';
    }
    write_if_changed(response, files);
    command = {target.link.front(),
               "-static",
               "-o",
               target.output.string(),
               "-filelist",
               response.string()};
  } else
#endif
  {
    const std::vector arguments(target.link.begin() + 1, target.link.end());
    std::string text;
    if (graph.toolchain.msvc)
      text = command_line(arguments);
    else {
      // GNU/LLVM response files use shell-like quoted words, independently of
      // the host shell. llvm-ar receives --rsp-quoting=posix where needed.
      for (const auto &argument : arguments) {
        text += '"';
        for (const char character : argument) {
          if (character == '\\' || character == '"')
            text += '\\';
          text += character;
        }
        text += "\"\n";
      }
    }
    write_if_changed(response, text);
    command = {target.link.front()};
    if (fs::path(target.link.front()).stem() == "llvm-ar")
      command.emplace_back("--rsp-quoting=posix");
    command.push_back("@" + response.string());
  }

  // ar replaces listed members but keeps omitted ones, so recreate the archive.
  // Removing only this validated output also leaves unrelated files untouched.
  fs::remove(target.output);
  const auto result = run(command, graph.project.build);
  if (result)
    fs::remove(target.output);
  return result;
}
}  // namespace clutch
