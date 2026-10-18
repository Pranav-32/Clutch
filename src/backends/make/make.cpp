/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Serialize native GNU Make, NMake and JOM rules, response files and
 * target selection without relying on another build generator.
 */

#include "make.hpp"

#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/targets/archive.hpp"
#include "core/targets/runtime.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clutch {
namespace {
/// Invalidate stale results before publishing a changed command. Timestamp-only
/// dependencies can miss edits within one clock tick on older Make versions.
void write_response(const fs::path &path,
                    const std::string &content,
                    const fs::path &output) {
  if (std::ifstream input(path, std::ios::binary); input) {
    const std::string previous((std::istreambuf_iterator(input)), {});
    if (input.bad())
      throw std::runtime_error("cannot read Make response file: " +
                               path.string());

    if (previous == content)
      return;
  }

  // Only remove this generated artifact, never a redirected destination. Do
  // this before writing metadata so interrupted configuration can be retried.
  if (fs::is_symlink(fs::symlink_status(output)) ||
      fs::canonical(output.parent_path()) != output.parent_path())
    throw std::runtime_error("refusing redirected Make output: " +
                             output.string());

  fs::remove(output);
  write_if_changed(path, content);
}

/// Reject characters that would split a rule/recipe or truncate a native
/// argument.
void validate_text(const std::string &value) {
  if (value.find_first_of("\r\n") != std::string::npos ||
      value.find('\0') != std::string::npos)
    throw std::runtime_error(
        "newlines and NUL bytes are not supported in Make arguments");
}

/// Commands pass through Make expansion before reaching the host shell.
std::string recipe(const std::vector<std::string> &arguments) {
  const auto command = command_line(arguments);
  validate_text(command);
  std::string result;

  for (const char c : command) {
    if (c == '$')
      result += '$';

    result += c;
  }

  return result;
}

/// Use relative paths to avoid drive letters in Make rule target names.
std::string relative_path(const fs::path &path, const fs::path &build) {
  const auto relative = path.lexically_relative(build);
  return relative.empty() ? path.generic_string() : relative.generic_string();
}

/// GNU Make escapes whitespace; NMake/JOM use quoted filenames instead.
std::string
make_path(const fs::path &path, const fs::path &build, const bool gnu) {
  const auto value = relative_path(path, build);
  validate_text(value);
  std::string result;

  if (!gnu)
    result += '"';

  for (const char c : value) {
    if (c == '$')
      result += '$';

    if (gnu && (c == ' ' || c == '\t' || c == '#' || c == ':' || c == '\\'))
      result += '\\';

    if (!gnu && c == '#')
      result += '^';

    result += c;
  }

  if (!gnu)
    result += '"';

  return result;
}

/// GNU drivers and Microsoft drivers have different response-file grammars.
std::string response(const std::vector<std::string> &arguments,
                     const bool msvc) {
  std::string result;

  for (size_t index = 1; index < arguments.size(); ++index) {
    const auto &argument = arguments[index];
    validate_text(argument);
    result += '"';

    if (!msvc) {
      for (const char c : argument) {
        if (c == '\\' || c == '"')
          result += '\\';

        result += c;
      }
    } else {
      size_t slashes = 0;

      for (const char c : argument) {
        if (c == '\\') {
          ++slashes;
          continue;
        }

        result.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        slashes = 0;
        result += c;
      }

      result.append(slashes * 2, '\\');
    }

    result += "\"\n";
  }

  return result;
}

/// Include common textual-header suffixes and extensionless forwarding headers.
bool is_header(const fs::path &path) {
  const auto extension = path.extension().string();
  return extension.empty() || extension == ".h" || extension == ".hh" ||
         extension == ".hpp" || extension == ".hxx" || extension == ".inc" ||
         extension == ".inl" || extension == ".ipp" || extension == ".tpp";
}

/// MSVC does not emit GNU dependency files. Conservatively attach all project
/// headers in source/include roots to every unit. Compiler installation headers
/// are not watched; changing the compiler installation requires a clean build.
std::set<fs::path> collect_headers(const Graph &graph) {
  std::set roots(graph.project.includes.begin(), graph.project.includes.end());

  for (const auto &target : graph.targets) {
    roots.insert(target.includes.begin(), target.includes.end());
    for (const auto &unit : target.units)
      roots.insert(unit.source.parent_path());
  }

  std::set<fs::path> headers;

  for (const auto &root : roots) {
    if (!fs::is_directory(root))
      continue;

    for (auto iterator = fs::recursive_directory_iterator(root);
         iterator != fs::recursive_directory_iterator();
         ++iterator) {
      const auto &entry = *iterator;

      if (entry.is_directory()) {
        if (const auto name = entry.path().filename().string();
            entry.is_symlink() || entry.path() == graph.project.build ||
            name == ".git" || name == layout::environment ||
            name.rfind(layout::build_prefix, 0) == 0)
          iterator.disable_recursion_pending();

        continue;
      }

      if (entry.is_regular_file() && is_header(entry.path()))
        headers.insert(entry.path());
    }
  }

  return headers;
}

/// Reconstruct positional configuration with the recorded profile and host
/// tools.
std::vector<std::string> regenerate_command(const Graph &graph,
                                            const Options &options,
                                            const fs::path &executable) {
  std::vector<std::string> arguments{executable.string(),
                                     graph.project.root.string(),
                                     "--build-tool",
                                     graph.backend.executable,
                                     "--generator",
                                     graph.backend.name};

  if (!graph.toolchain.cc.empty())
    arguments.insert(arguments.end(), {"--cc", graph.toolchain.cc});

  if (!graph.toolchain.cxx.empty())
    arguments.insert(arguments.end(), {"--cxx", graph.toolchain.cxx});

  if (options.release)
    arguments.emplace_back("--release");
  if (options.locked)
    arguments.emplace_back("--locked");

  return arguments;
}
}  // namespace

/// Emit one native Make dialect while preserving the graph's compile/link
/// commands.
void generate_make(const Graph &graph,
                   const Options &options,
                   const fs::path &clutch_executable) {
  const bool gnu = graph.backend.name == "make";
  const auto &build = graph.project.build;
  // GNU drivers provide depfiles; MSVC needs conservative explicit header
  // edges.
  const auto headers = gnu && !graph.toolchain.msvc ? std::set<fs::path>{}
                                                    : collect_headers(graph);
  std::ostringstream make;
  make << "# Generated by Clutch. Do not edit.\n"
          "# Reconfigure with clutch <project> after changing project "
          "settings.\n";

  if (gnu) {
    make << ".DEFAULT_GOAL := clutch_all\n"
            ".DELETE_ON_ERROR:\n"
            ".SUFFIXES:\n"
            ".PHONY: clutch_all clutch-tests";

    for (size_t index = 0; index < graph.targets.size(); ++index)
      make << " clutch_target_" << index;

    make << "\n";
  } else {
    make
        << "# NMake/JOM conservatively rebuild units when scanned headers "
           "change.\n"
           "# Reconfigure before building after manifest/source-list changes.\n"
           ".SUFFIXES:\n";
  }

  // Stable synthetic target names avoid exposing platform-specific output
  // paths.
  make << "\nclutch_all:";
  for (size_t index = 0; index < graph.targets.size(); ++index)
    if (graph.targets[index].build_by_default && !graph.targets[index].is_test)
      make << " clutch_target_" << index;
  make << "\n\nclutch-tests:";
  for (size_t index = 0; index < graph.targets.size(); ++index)
    if (graph.targets[index].is_test)
      make << " clutch_target_" << index;
  make << "\n\n";

  // Import libraries are secondary linker outputs. NMake treats a recipe as
  // an update even when it leaves a file unchanged; depend on the DLL normally
  // and request repair only if the import library is absent at parse time.
  for (size_t index = 0; index < graph.targets.size(); ++index) {
    const auto &library = graph.targets[index];
    if (library.import_library.empty())
      continue;

    if (gnu)
      make << "ifeq ($(wildcard "
           << make_path(library.import_library, build, true) << "),)\n";
    else
      make << "!IF !EXIST(" << make_path(library.import_library, build, false)
           << ")\n";
    make << "clutch_import_" << index << " = "
         << make_path(library.import_library, build, gnu) << '\n'
         << (gnu ? "else\n" : "!ELSE\n") << "clutch_import_" << index << " =\n"
         << (gnu ? "endif\n\n" : "!ENDIF\n\n");
  }

  std::set<fs::path> staged;
  for (size_t index = 0; index < graph.targets.size(); ++index) {
    const auto &target = graph.targets[index];
    const auto &name = target.name;
    const auto &output = target.output;
    const auto &units = target.units;
    const auto &link = target.link;
    fs::create_directories(output.parent_path());
    const auto copies = runtime_copies(graph, index);
    make << "clutch_target_" << index << ": " << make_path(output, build, gnu);
    for (const auto &[library, destination] : copies) {
      (void)library;
      make << ' ' << make_path(destination, build, gnu);
    }

    make << "\n\n";
    for (const auto &[library, destination] : copies) {
      if (!staged.insert(destination).second)
        continue;

      make << make_path(destination, build, gnu) << ": "
           << make_path(graph.targets[library].output, build, gnu) << "\n\t"
           << recipe(runtime_copy_command(graph,
                                          options,
                                          clutch_executable,
                                          index,
                                          library))
           << "\n\n";
    }

    for (const auto &unit : units) {
      if (unit.arguments.empty())
        throw std::runtime_error("Make compile unit has no compiler command");

      fs::create_directories(unit.object.parent_path());
      const auto response_path = fs::path(unit.object.string() + ".make.rsp");

      // Copy only when adding generator-specific dependency flags.
      auto arguments = unit.arguments;

      if (gnu && !graph.toolchain.msvc) {
        // Match the Make rule's relative target and tolerate removed headers.
        arguments.insert(arguments.end(),
                         {"-MP", "-MQ", relative_path(unit.object, build)});
      }

      // Keep timestamp prerequisites for native builds and invalidate old
      // objects when the command changes, even within one timestamp tick.
      write_response(response_path,
                     response(arguments, graph.toolchain.msvc),
                     unit.object);
      make << make_path(unit.object, build, gnu) << ": "
           << make_path(unit.source, build, gnu) << ' '
           << make_path(response_path, build, gnu);

      for (const auto &header : headers)
        make << " \\\n  " << make_path(header, build, gnu);

      make << "\n\t@echo Compiling " << unit.object.filename().string()
           << "\n\t"
           << recipe({unit.arguments.front(),
                      "@" + relative_path(response_path, build)})
           << "\n\n";
    }

    if (link.empty())
      throw std::runtime_error("Make target has no linker command");

    const auto response_path = fs::path(output.string() + ".make.rsp");
    write_response(response_path, response(link, graph.toolchain.msvc), output);
    make << make_path(output, build, gnu) << ": "
         << make_path(response_path, build, gnu);

    for (const auto &unit : units)
      make << " \\\n  " << make_path(unit.object, build, gnu);

    for (const auto dependency : target.type == TargetType::StaticLibrary
                                     ? target.dependencies
                                     : target.link_dependencies) {
      const auto &library = graph.targets.at(dependency);
      make << " \\\n  " << make_path(library.output, build, gnu);
      if (!library.import_library.empty())
        make << " $(clutch_import_" << dependency << ')';
    }
    const auto command =
        target.type == TargetType::StaticLibrary
            ? archive_command(graph, options, clutch_executable, index)
            : std::vector<std::string>{link.front(),
                                       "@" +
                                           relative_path(response_path, build)};
    make << "\n\t@echo "
         << (target.type == TargetType::StaticLibrary ? "Archiving "
                                                      : "Linking ")
         << name << "\n\t" << recipe(command) << "\n\n";
    if (!target.import_library.empty()) {
      // NMake has no grouped-output rule. The DLL is the sole producer; repair
      // its import library only when that secondary output has been removed.
      make << make_path(target.import_library, build, gnu) << ": "
           << make_path(output, build, gnu) << "\n\t@if not exist "
           << make_path(target.import_library, build, false) << ' '
           << recipe(command) << "\n\n";
    }
  }

  if (gnu) {
    // Missing depfiles are normal before the first build; include them
    // optionally.
    if (!graph.toolchain.msvc) {
      for (const auto &target : graph.targets) {
        for (const auto &unit : target.units)
          make << "-include "
               << make_path(fs::path(unit.object.string() + ".d"), build, gnu)
               << "\n";
      }

      make << "\n";
    }

    // GNU Make remakes an out-of-date loaded Makefile and reparses its new
    // rules.
    make << "Makefile:";
    for (const auto &[input, signature] : graph.project.inputs) {
      (void)signature;
      make << ' ' << make_path(input, build, gnu);
    }
    make << ' ' << make_path(clutch_executable, build, gnu) << "\n\t"
         << recipe(regenerate_command(graph, options, clutch_executable))
         << "\n";
  }

  write_if_changed(build / "Makefile", make.str());
}

/// Apply each Make dialect's invocation and parallelism rules to selected
/// targets.
std::vector<std::string>
make_build_command(const Graph &graph,
                   const Options &options,
                   const std::vector<size_t> &target_indices) {
  const bool gnu = graph.backend.name == "make";
  std::vector arguments{graph.backend.executable};

  if (gnu)
    arguments.insert(arguments.end(),
                     {"-C", graph.project.build.string(), "-f", "Makefile"});
  else
    arguments.insert(arguments.end(), {"/NOLOGO", "/F", "Makefile"});

  if (options.jobs && (gnu || graph.backend.name == "jom"))
    arguments.insert(arguments.end(), {"-j", std::to_string(options.jobs)});

  if (target_indices.empty())
    arguments.emplace_back("clutch_target_0");

  for (const auto index : target_indices) {
    if (index >= graph.targets.size())
      throw std::runtime_error("Make target index is out of range");

    arguments.emplace_back("clutch_target_" + std::to_string(index));
  }

  return arguments;
}
}  // namespace clutch
