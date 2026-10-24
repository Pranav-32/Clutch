/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Discover bundled Ninja tools and generate rules from the resolved
 * graph, including response files and regeneration.
 */

#include "ninja.hpp"
#include "backends/foreign_actions.hpp"
#include "core/dependencies/foreign.hpp"
#include "core/pch/pch.hpp"
#include "platform/arguments.hpp"

#include "core/build_graph.hpp"
#include "core/configured_files/configured_files.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/targets/archive.hpp"
#include "core/targets/runtime.hpp"
#include "platform/environment.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clutch {
namespace {
#ifdef _WIN32
// Enumerate only known installation levels. Missing or unreadable optional
// installation folders must not prevent discovery of another installed tool.
std::vector<fs::path> installation_directories(const fs::path &root) {
  std::vector<fs::path> result;
  std::error_code error;
  fs::directory_iterator current(root,
                                 fs::directory_options::skip_permission_denied,
                                 error);
  const fs::directory_iterator end;
  for (; !error && current != end; current.increment(error)) {
    if (std::error_code entry_error; current->is_directory(entry_error))
      result.push_back(current->path());
  }

  // A stable order avoids changing tools between identical configure runs.
  std::sort(result.begin(), result.end());
  return result;
}

/// Return a Ninja executable from one known bundle location, or an empty
/// string.
std::string bundled_ninja(const fs::path &directory) {
  const auto executable = directory / "ninja.exe";
  if (std::error_code error; fs::is_regular_file(executable, error))
    return fs::absolute(executable).u8string();

  return {};
}
#endif

/// Escape Ninja variable expansion separately from shell/response-file quoting.
std::string ninja_value(const std::string &value) {
  std::string result;
  for (const char c : value) {
    if (c == '\n' || c == '\r' || c == '\0')
      throw std::runtime_error(
          "newlines and NUL bytes are not allowed in build arguments");

    if (c == '$')
      result += '$';

    result += c;
  }

  return result;
}

/// Encode a path token using Ninja's dollar escapes for spaces and separators.
std::string path(const fs::path &p) {
  std::string result;
  for (const char c : p.generic_u8string()) {
    if (c == '\n' || c == '\r')
      throw std::runtime_error("newlines are not supported in paths");

    if (c == '$' || c == ' ' || c == ':')
      result += '$';

    result += c;
  }

  return result;
}

}  // namespace

/// Probe Windows IDE bundles only after normal executor resolution has failed.
std::string discover_ninja() {
#ifdef _WIN32
  // Visual Studio installs CMake and Ninja in adjacent package directories.
  const fs::path visual_studio_ninja =
      "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja";
  if (const auto installation = environment_value("VSINSTALLDIR")) {
    if (const auto executable =
            bundled_ninja(fs::u8path(*installation) / visual_studio_ninja);
        !executable.empty())
      return executable;
  }

  if (const auto cmake = find_program("cmake"); !cmake.empty()) {
    const auto directory = fs::u8path(cmake).parent_path();
    for (const auto &candidate :
         {directory, directory.parent_path().parent_path() / "Ninja"}) {
      if (const auto executable = bundled_ninja(candidate); !executable.empty())
        return executable;
    }
  }

  // Default Windows installation roots cover standalone Build Tools and IDE
  // packages without pinning a product version, edition or installation drive.
  constexpr std::array installation_roots{"ProgramFiles", "ProgramFiles(x86)"};
  constexpr std::array clion_ninja_paths{"bin/ninja/win/x64",
                                         "bin/ninja/win/aarch64"};
  for (const auto *const variable : installation_roots) {
    if (const auto root = environment_value(variable)) {
      for (const auto &version : installation_directories(
               fs::u8path(*root) / "Microsoft Visual Studio")) {
        for (const auto &edition : installation_directories(version)) {
          if (const auto executable =
                  bundled_ninja(edition / visual_studio_ninja);
              !executable.empty())
            return executable;
        }
      }

      for (const auto &installation :
           installation_directories(fs::u8path(*root) / "JetBrains")) {
        if (installation.filename().u8string().rfind("CLion", 0) != 0)
          continue;

        for (const auto *const relative : clion_ninja_paths) {
          if (const auto executable = bundled_ninja(installation / relative);
              !executable.empty())
            return executable;
        }
      }
    }
  }
#endif
  return {};
}

/// Write compile/link rules, dependency tracking and manifest regeneration
/// edges.
void generate_ninja(const Graph &graph,
                    const Options &options,
                    const fs::path &executable) {
  const auto &p = graph.project;
  const auto &cc = graph.toolchain.cc;
  const auto &cxx = graph.toolchain.cxx;
  std::ostringstream ninja;
  ninja
      << "# Generated by Clutch. Do not edit.\nninja_required_version = 1.10\n";
  // Metadata-only edits must also change the generated file. Otherwise an
  // unchanged rule set can retain an mtime older than its configuration inputs.
  for (const auto &[input, signature] : p.inputs)
    ninja << "# input " << signature << ' ' << path(input) << '\n';

  // Response files preserve long argument lists; each driver supplies header
  // deps.
  ninja << "rule compile\n  command = $compiler @$out.rsp\n  rspfile = "
           "$out.rsp\n  "
           "rspfile_content = $args\n  description = Compiling $in\n";
  if (graph.toolchain.msvc)
    ninja << "  deps = msvc\n";
  else
    ninja << "  deps = gcc\n  depfile = $out.d\n";

  ninja << "rule link\n  command = $compiler @\"$artifact.rsp\"\n  rspfile = "
           "$artifact.rsp\n"
        << "  rspfile_content = $args\n  description = Linking $artifact\n";
  ninja << "rule archive\n  command = $archive_command\n  description = "
           "Archiving $out\n";
  ninja << "rule runtime_copy\n  command = $copy_command\n  description = "
           "Staging $out\n";
  std::set<fs::path> staged;

  if (!p.configured_files.empty()) {
    ninja << "rule configure_files\n  command = "
          << ninja_value(command_line(
                 configured_files_command(graph, options, executable)))
          << "\n  description = Refreshing configured files\n  restat = 1\n"
             "build clutch-configure-force: phony\nbuild";
    for (const auto &file : p.configured_files)
      // Ninja's MSVC include parser records headers under its cwd relatively.
      // Use that exact node spelling, so dynamic deps share the producer edge.
      ninja << ' '
            << path(graph.toolchain.msvc
                        ? file.output.lexically_relative(p.build)
                        : file.output);

    ninja << ": configure_files clutch-configure-force\n"
             "build clutch-configured-files: phony";
    for (const auto &file : p.configured_files)
      ninja << ' '
            << path(graph.toolchain.msvc
                        ? file.output.lexically_relative(p.build)
                        : file.output);

    ninja << "\n\n";
  }

  if (!p.foreign_builds.empty() ||
      std::any_of(graph.targets.begin(),
                  graph.targets.end(),
                  [](const Target &target) {
                    return !target.runtime_libraries.empty();
                  })) {
    ninja << "pool clutch_foreign\n  depth = 1\n"
             "rule foreign\n  command = $prepare_command\n"
             "  description = Preparing native dependency\n  restat = 1\n"
             "  pool = clutch_foreign\n"
             "rule foreign_runtime\n  command = $prepare_command\n"
             "  description = Staging imported runtime libraries\n"
             "build clutch-foreign-force: phony\n";
    for (const auto &action : p.foreign_builds) {
      ninja << "build " << path(dependencies::foreign_stamp(graph, action.id));
      if (!action.artifacts.empty()) {
        ninja << " |";
        for (const auto &artifact : action.artifacts)
          ninja << ' ' << path(artifact);
      }
      ninja
          << ": foreign clutch-foreign-force\n  prepare_command = "
          << ninja_value(command_line(
                 foreign_action_command(graph, options, executable, action.id)))
          << '\n';
    }
  }

  // Every application/test unit keeps its graph-selected compiler and object
  // path.
  for (size_t target_index = 0; target_index < graph.targets.size();
       ++target_index) {
    const auto &target = graph.targets[target_index];
    fs::create_directories(target.output.parent_path());

    // Imported headers can be generated only by an upstream build action.
    // A restat stamp changes for public-header membership/timestamps as well as
    // archives, including header-only CMake targets with no link artifact.
    const auto foreign_prerequisites = [&] {
      for (const auto &id : target.foreign_dependencies)
        ninja << ' ' << path(dependencies::foreign_stamp(graph, id));
    };
    const bool foreign_runtime = target.type == TargetType::Executable &&
                                 !target.runtime_libraries.empty();
    if (foreign_runtime) {
      ninja << "build clutch_foreign_runtime_" << target_index
            << ": foreign_runtime clutch-foreign-force";
      if (!target.foreign_dependencies.empty())
        ninja << " ||";
      foreign_prerequisites();
      ninja << "\n  prepare_command = "
            << ninja_value(command_line(foreign_runtime_command(graph,
                                                                options,
                                                                executable,
                                                                target_index)))
            << '\n';
    }

    for (const auto &prefix : target.precompiled_headers) {
      fs::create_directories(prefix.creator.object.parent_path());
      ninja << "build " << path(prefix.creator.object);
      if (!prefix.companion_object.empty())
        ninja << " | " << path(prefix.companion_object);

      ninja << ": compile " << path(prefix.creator.source) << " | "
            << path(fs::u8path(prefix.creator.arguments.front()));
      if (prefix.creator.source != prefix.header)
        ninja << ' ' << path(prefix.header);
      foreign_prerequisites();

      if (!p.configured_files.empty()) {
        ninja << " || clutch-configured-files";
      }

      ninja << "\n  compiler = "
            << ninja_value(command_line({prefix.creator.arguments.front()}))
            << "\n  args = "
            << ninja_value(response_file(prefix.creator.arguments,
                                         graph.toolchain.msvc))
            << '\n';
    }

    for (const auto &unit : target.units) {
      const auto &[source, object, ide_arguments] = unit;
      (void)ide_arguments;
      const auto arguments = pch::build_arguments(target, unit);
      fs::create_directories(object.parent_path());
      ninja << "build " << path(object) << ": compile " << path(source);
      if (const auto *const prefix = pch::for_unit(target, unit))
        ninja << " | " << path(prefix->creator.object);
      else if (!target.foreign_dependencies.empty())
        ninja << " |";
      foreign_prerequisites();

      if (!p.configured_files.empty()) {
        ninja << " || clutch-configured-files";
      }

      ninja << "\n  compiler = "
            << ninja_value(command_line({arguments.front()})) << "\n  args = "
            << ninja_value(response_file(arguments, graph.toolchain.msvc))
            << "\n";
    }

    const auto copies = runtime_copies(graph, target_index);
    for (const auto &[library, destination] : copies) {
      if (!staged.insert(destination).second)
        continue;

      ninja << "build " << path(destination) << ": runtime_copy "
            << path(graph.targets[library].output) << "\n  copy_command = "
            << ninja_value(command_line(runtime_copy_command(graph,
                                                             options,
                                                             executable,
                                                             target_index,
                                                             library)))
            << "\n";
    }

    ninja << "build " << path(target.output);
    if (!target.import_library.empty())
      ninja << " | " << path(target.import_library);
    ninja << (target.type == TargetType::StaticLibrary ? ": archive"
                                                       : ": link");
    for (const auto &unit : target.units)
      ninja << ' ' << path(unit.object);
    for (const auto &prefix : target.precompiled_headers)
      if (!prefix.companion_object.empty())
        ninja << ' ' << path(prefix.companion_object);
    foreign_prerequisites();
    if (target.type == TargetType::StaticLibrary) {
      const auto metadata =
          p.build / "obj" /
          ("archive-" + std::to_string(target_index) + ".command");
      write_if_changed(metadata, command_line(target.link) + '\n');
      ninja << " | " << path(metadata);
    }
    if (target.type != TargetType::StaticLibrary) {
      for (const auto &input : target.link_inputs)
        ninja << ' ' << path(input);
      for (const auto dependency : target.link_dependencies) {
        const auto &library = graph.targets.at(dependency);
        ninja << ' '
              << path(library.import_library.empty() ? library.output
                                                     : library.import_library);
      }
    } else if (!target.dependencies.empty()) {
      ninja << " ||";
      for (const auto dependency : target.dependencies)
        ninja << ' ' << path(graph.targets.at(dependency).output);
    }
    if (!copies.empty() || foreign_runtime) {
      ninja << " ||";
      for (const auto &[library, destination] : copies) {
        (void)library;
        ninja << ' ' << path(destination);
      }
      if (foreign_runtime)
        ninja << " clutch_foreign_runtime_" << target_index;
    }

    ninja << "\n";
    if (target.type == TargetType::StaticLibrary)
      ninja << "  archive_command = "
            << ninja_value(command_line(
                   archive_command(graph, options, executable, target_index)))
            << "\n";
    else
      ninja << "  artifact = " << ninja_value(target.output.u8string())
            << "\n  compiler = "
            << ninja_value(command_line({target.link.front()})) << "\n  args = "
            << ninja_value(response_file(target.link, graph.toolchain.msvc))
            << "\n";
  }

  // Tests are opt-in; the default goal below remains the application
  // executable.
  ninja << "build clutch-tests: phony";
  for (size_t i = 0; i < graph.targets.size(); ++i)
    if (graph.targets[i].is_test)
      ninja << ' ' << path(graph.targets[i].output);

  ninja << "\n";

  // Direct Ninja builds can refresh a changed manifest using these exact tools.
  // Source-list discovery still requires an explicit configuration pass.
  std::vector<std::string> regenerate{executable.u8string(),
                                      p.root.u8string(),
                                      "--build-tool",
                                      graph.backend.executable,
                                      "--generator",
                                      p.generator};
  if (!graph.toolchain.cxx.empty())
    regenerate.insert(regenerate.end(), {"--cxx", cxx});

  if (!graph.toolchain.cc.empty())
    regenerate.insert(regenerate.end(), {"--cc", cc});

  if (options.release)
    regenerate.emplace_back("--release");
  if (options.locked)
    regenerate.emplace_back("--locked");
  if (options.offline)
    regenerate.emplace_back("--offline");
  if (options.update_dependencies)
    regenerate.emplace_back("--auto-update-dependencies");

  ninja << "rule regenerate\n  command = "
        << ninja_value(command_line(regenerate))
        << "\n  generator = 1\n  restat = 1\n  description = Configuring "
        << p.name << "\n";
  ninja << "build build.ninja: regenerate";
  for (const auto &[input, signature] : p.inputs) {
    (void)signature;
    ninja << ' ' << path(input);
  }
  ninja << "\n";
  const auto defaults = p.build / layout::environment / "default-targets";
  ninja << "build " << path(defaults) << ": phony";
  for (const auto &target : graph.targets)
    if (target.build_by_default && !target.is_test)
      ninja << ' ' << path(target.output);
  ninja << "\ndefault " << path(defaults) << "\n";

  write_if_changed(p.build / "build.ninja", ninja.str());
}
}  // namespace clutch
