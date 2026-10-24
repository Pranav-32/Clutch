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
#include "backends/foreign_actions.hpp"
#include "core/dependencies/foreign.hpp"
#include "core/pch/pch.hpp"
#include "platform/arguments.hpp"

#include "core/build_graph.hpp"
#include "core/encoding_constants.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/targets/archive.hpp"
#include "core/targets/runtime.hpp"
#include "core/workspace/inputs.hpp"
#include "platform/capture.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clutch {
/// Older GNU Make versions truncate prerequisite timestamps to whole seconds.
/// Refuse those executors rather than silently reusing stale archives or PCH.
void validate_gnu_make(const std::string &executable,
                       const fs::path &directory) {
  constexpr auto minimum_make_major = 4U;
  const auto result = capture({executable, "--version"}, directory);
  std::istringstream version(result.text);
  std::string vendor, program;
  unsigned major = 0;
  version >> vendor >> program >> major;
  if (result.status || vendor != "GNU" || program != "Make" ||
      major < minimum_make_major)
    throw std::runtime_error(
        "the Make generator requires GNU Make 4 or newer; configure with "
        "--build-tool gmake or select Ninja");
}

namespace {

/// Removing a generated result is safe only at its physical, owned location.
void invalidate_output(const fs::path &output) {
  if (fs::is_symlink(fs::symlink_status(output)) ||
      fs::weakly_canonical(output.parent_path()) != output.parent_path())
    throw std::runtime_error("refusing redirected Make output: " +
                             output.u8string());

  fs::remove(output);
}

/// Invalidate stale results before publishing a changed command. Timestamp-only
/// dependencies can miss edits within one clock tick on older Make versions.
void write_response(const fs::path &path,
                    const std::string &content,
                    const fs::path &output,
                    const std::function<void()> &invalidate_dependents) {
  if (std::ifstream input(path, std::ios::binary); input) {
    if (const std::string previous((std::istreambuf_iterator(input)), {});
        input.bad())
      throw std::runtime_error("cannot read Make response file: " +
                               path.u8string());
    else if (previous == content)
      return;
  }

  // Invalidate the full chain before writing metadata. A rebuilt object's mtime
  // can still equal an older archive's mtime on a coarse clock.
  invalidate_output(output);
  invalidate_dependents();
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
  return relative.empty() ? path.generic_u8string()
                          : relative.generic_u8string();
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

/// Include common textual-header suffixes and extensionless forwarding headers.
bool is_header(const fs::path &path) {
  const auto extension = path.extension().u8string();
  return extension.empty() || extension == ".h" || extension == ".hh" ||
         extension == ".hpp" || extension == ".hxx" || extension == ".inc" ||
         extension == ".inl" || extension == ".ipp" || extension == ".tpp";
}

/// MSVC does not emit GNU dependency files. Conservatively attach all project
/// headers in source/include roots to every unit. Compiler installation headers
/// are not watched; changing the compiler installation requires a clean build.
std::set<fs::path> collect_headers(const Graph &graph) {
  std::set roots(graph.project.includes.begin(), graph.project.includes.end());
  std::set<fs::path> headers;

  for (const auto &target : graph.targets) {
    // Explicit header selections may live outside source/include roots.
    for (const auto &file : target.additional_files)
      if (is_header(file))
        headers.insert(file);

    roots.insert(target.includes.begin(), target.includes.end());
    for (const auto &unit : target.units) {
      roots.insert(unit.source.parent_path());
      for (const auto &argument : unit.arguments)
        if ((argument.rfind("-I", 0) == 0 || argument.rfind("/I", 0) == 0) &&
            argument.size() > 2)
          roots.insert(fs::u8path(argument.substr(2)));
    }
  }

  for (const auto &root : roots) {
    if (!fs::is_directory(root))
      continue;

    for (auto iterator = fs::recursive_directory_iterator(root);
         iterator != fs::recursive_directory_iterator();
         ++iterator) {
      const auto &entry = *iterator;

      if (entry.is_directory()) {
        if (const auto name = entry.path().filename().u8string();
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
  std::vector<std::string> arguments{executable.u8string(),
                                     graph.project.root.u8string(),
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
  if (options.offline)
    arguments.emplace_back("--offline");
  if (options.update_dependencies)
    arguments.emplace_back("--auto-update-dependencies");

  return arguments;
}
}  // namespace

/// Emit one native Make dialect while preserving the graph's compile/link
/// commands.
void generate_make(const Graph &graph,
                   const Options &options,
                   const fs::path &clutch_executable) {
  const bool gnu = graph.backend.name == "make";
  if (gnu)
    validate_gnu_make(graph.backend.executable, graph.project.root);

  const auto &build = graph.project.build;
  // Reverse edges let an altered compile/archive/link command invalidate every
  // consuming output, independent of graph order and timestamp precision.
  std::vector<std::vector<size_t>> consumers(graph.targets.size());
  for (size_t index = 0; index < graph.targets.size(); ++index)
    for (const auto provider : graph.targets[index].dependencies)
      consumers.at(provider).push_back(index);

  std::vector<bool> invalidated(graph.targets.size(), false);
  const auto invalidate_target = [&](const size_t first) {
    std::queue<size_t> pending;
    pending.push(first);
    while (!pending.empty()) {
      const auto index = pending.front();
      pending.pop();
      if (invalidated[index])
        continue;

      invalidate_output(graph.targets[index].output);
      for (const auto &[library, destination] : runtime_copies(graph, index)) {
        (void)library;
        invalidate_output(destination);
      }
      invalidated[index] = true;
      for (const auto consumer : consumers[index])
        pending.push(consumer);
    }
  };

  // GNU drivers provide depfiles; MSVC needs conservative explicit header
  // edges.
  const auto headers = gnu && !graph.toolchain.msvc ? std::set<fs::path>{}
                                                    : collect_headers(graph);
  std::ostringstream make;
  // NMake otherwise decodes non-ASCII prerequisite names through ANSI.
  if (graph.backend.name == "nmake")
    make << encoding::utf8_bom;

  make << "# Generated by Clutch. Do not edit.\n"
          "# Reconfigure with clutch <project> after changing project "
          "settings.\n";
  // Record metadata-only changes too, so Make's remake/reparse cycle publishes
  // a new snapshot rather than reconfiguring the same old Makefile every run.
  for (const auto &[input, signature] : graph.project.inputs)
    make << "# input " << signature << ' ' << make_path(input, build, gnu)
         << '\n';

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

  if (gnu && !graph.project.foreign_builds.empty())
    make << ".PHONY: clutch_foreign_force\nclutch_foreign_force:\n\n";
  for (const auto &action : graph.project.foreign_builds) {
    const auto stamp = dependencies::foreign_stamp(graph, action.id);
    const auto command = recipe(
        foreign_action_command(graph, options, clutch_executable, action.id));
    if (gnu)
      make << make_path(stamp, build, true) << ": clutch_foreign_force\n\t"
           << command << "\n\n";
    else
      // NMake has no order-only prerequisites/restat. Run preparation before
      // timestamp evaluation, preserving unchanged object and link products.
      make << "!IF [" << command
           << "] != 0\n!ERROR Native dependency preparation failed\n!ENDIF\n\n";

    for (const auto &artifact : action.artifacts)
      make << make_path(artifact, build, gnu) << ": "
           << make_path(stamp, build, gnu) << "\n\n";
  }

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
    const auto foreign_prerequisites = [&] {
      for (const auto &id : target.foreign_dependencies)
        make << ' '
             << make_path(dependencies::foreign_stamp(graph, id), build, gnu);
    };
    const auto &link = target.link;
    fs::create_directories(output.parent_path());
    const auto copies = runtime_copies(graph, index);
    make << "clutch_target_" << index << ": " << make_path(output, build, gnu);
    for (const auto &[library, destination] : copies) {
      (void)library;
      make << ' ' << make_path(destination, build, gnu);
    }

    make << "\n\n";
    if (target.type == TargetType::Executable &&
        !target.runtime_libraries.empty()) {
      // Aggregate target recipes run after their output prerequisites, even
      // when linking is already current, repairing deleted runtime copies.
      make << "clutch_target_" << index << ":\n\t"
           << recipe(foreign_runtime_command(graph,
                                             options,
                                             clutch_executable,
                                             index))
           << "\n\n";
    }
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

    for (const auto &prefix : target.precompiled_headers) {
      const auto &creator = prefix.creator;
      fs::create_directories(creator.object.parent_path());
      // A changed prefix list can be written within the old PCH's timestamp
      // tick. Track its bytes explicitly and remove affected products before
      // Make parses prerequisites; unchanged configuration retains every
      // output.
      write_response(fs::u8path(creator.object.u8string() + ".make.prefix"),
                     input_signature(prefix.header),
                     creator.object,
                     [&] {
                       for (const auto &unit : units)
                         if (pch::for_unit(target, unit) == &prefix)
                           invalidate_output(unit.object);
                       invalidate_target(index);
                     });
      const auto response = fs::u8path(creator.object.u8string() + ".make.rsp");
      auto arguments = creator.arguments;
      if (gnu && !graph.toolchain.msvc)
        arguments.insert(arguments.end(),
                         {"-MP", "-MQ", relative_path(creator.object, build)});

      write_response(response,
                     response_file(arguments, graph.toolchain.msvc),
                     creator.object,
                     [&] {
                       invalidate_target(index);
                     });
      make << make_path(creator.object, build, gnu) << ": "
           << make_path(creator.source, build, gnu) << ' '
           << make_path(response, build, gnu) << ' '
           << make_path(fs::u8path(creator.arguments.front()), build, gnu);
      if (creator.source != prefix.header)
        make << ' ' << make_path(prefix.header, build, gnu);
      foreign_prerequisites();
      for (const auto &header : prefix.headers)
        make << " \\\n  " << make_path(header, build, gnu);
      for (const auto &header : headers)
        make << " \\\n  " << make_path(header, build, gnu);

      const auto command = recipe(
          {creator.arguments.front(), "@" + relative_path(response, build)});
      make << "\n\t" << command << "\n\n";
      if (!prefix.companion_object.empty())
        make << make_path(prefix.companion_object, build, gnu) << ": "
             << make_path(creator.object, build, gnu) << "\n\t@if not exist "
             << make_path(prefix.companion_object, build, false) << ' '
             << command << "\n\n";
    }

    for (const auto &unit : units) {
      if (unit.arguments.empty())
        throw std::runtime_error("Make compile unit has no compiler command");

      fs::create_directories(unit.object.parent_path());
      const auto response_path =
          fs::u8path(unit.object.u8string() + ".make.rsp");

      // Copy only when adding generator-specific dependency flags.
      auto arguments = pch::build_arguments(target, unit);

      if (gnu && !graph.toolchain.msvc) {
        // Match the Make rule's relative target and tolerate removed headers.
        arguments.insert(arguments.end(),
                         {"-MP", "-MQ", relative_path(unit.object, build)});
      }

      // Keep timestamp prerequisites for native builds and invalidate old
      // objects when the command changes, even within one timestamp tick.
      write_response(response_path,
                     response_file(arguments, graph.toolchain.msvc),
                     unit.object,
                     [&] {
                       invalidate_target(index);
                     });
      make << make_path(unit.object, build, gnu) << ": "
           << make_path(unit.source, build, gnu) << ' '
           << make_path(response_path, build, gnu);
      if (const auto *const prefix = pch::for_unit(target, unit))
        make << ' ' << make_path(prefix->creator.object, build, gnu);
      foreign_prerequisites();

      for (const auto &header : headers)
        make << " \\\n  " << make_path(header, build, gnu);

      make << "\n\t@echo Compiling " << unit.object.filename().u8string()
           << "\n\t"
           << recipe({unit.arguments.front(),
                      "@" + relative_path(response_path, build)})
           << "\n\n";
    }

    if (link.empty())
      throw std::runtime_error("Make target has no linker command");

    const auto response_path = fs::u8path(output.u8string() + ".make.rsp");
    write_response(response_path,
                   response_file(link, graph.toolchain.msvc),
                   output,
                   [&] {
                     invalidate_target(index);
                   });
    make << make_path(output, build, gnu) << ": "
         << make_path(response_path, build, gnu);
    foreign_prerequisites();
    for (const auto &input : target.link_inputs)
      make << " \\\n  " << make_path(input, build, gnu);

    for (const auto &unit : units)
      make << " \\\n  " << make_path(unit.object, build, gnu);
    for (const auto &prefix : target.precompiled_headers)
      if (!prefix.companion_object.empty())
        make << " \\\n  " << make_path(prefix.companion_object, build, gnu);

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
        for (const auto &prefix : target.precompiled_headers)
          make << "-include "
               << make_path(fs::u8path(prefix.creator.object.u8string() + ".d"),
                            build,
                            gnu)
               << '\n';
        for (const auto &unit : target.units)
          make << "-include "
               << make_path(fs::u8path(unit.object.u8string() + ".d"),
                            build,
                            gnu)
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
  if (gnu)
    validate_gnu_make(graph.backend.executable, graph.project.root);

  std::vector arguments{graph.backend.executable};
  if (options.clean_build) {
    if (gnu)
      // The saved graph has already validated its configuration inputs. Force
      // products without remaking the loaded Makefile and republishing IDE
      // data.
      arguments.insert(arguments.end(),
                       {"--always-make", "--assume-old=Makefile"});
    else
      arguments.emplace_back("/A");
  }

  if (gnu)
    arguments.insert(arguments.end(),
                     {"-C", graph.project.build.u8string(), "-f", "Makefile"});
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
