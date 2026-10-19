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
#include "platform/arguments.hpp"

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
  std::vector<std::string> command{executable.u8string(),
                                   "__archive",
                                   "--project",
                                   graph.project.root.u8string(),
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
  const auto response = fs::u8path(target.output.u8string() + ".archive.rsp");
  if (fs::is_symlink(response))
    throw std::runtime_error("refusing redirected archive response file");

#ifdef __APPLE__
  if (fs::u8path(target.link.front()).filename() == "libtool") {
    std::string files;
    for (const auto &unit : target.units) {
      if (unit.object.u8string().find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error(
            "newlines are not supported in archive file lists");
      files += unit.object.u8string() + '\n';
    }
    write_if_changed(response, files);
    command = {target.link.front(),
               "-static",
               "-o",
               target.output.u8string(),
               "-filelist",
               response.u8string()};
  } else
#endif
  {
    write_if_changed(response,
                     response_file(target.link, graph.toolchain.msvc));
    command = {target.link.front()};
    if (fs::u8path(target.link.front()).stem() == "llvm-ar")
      command.emplace_back("--rsp-quoting=posix");
    command.push_back("@" + response.u8string());
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
