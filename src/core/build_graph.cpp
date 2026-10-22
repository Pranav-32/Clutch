/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Construct backend-independent compile and link commands for
 * application and test targets.
 */

#include "build_graph.hpp"

#include "core/options.hpp"
#include "core/pch/pch.hpp"
#include "core/targets/requirements.hpp"
#include "core/targets/resolve.hpp"
#include "core/targets/runtime.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace clutch {
namespace {
/// Windows artifact identity is case-insensitive even when manifest names
/// differ.
fs::path artifact_key(const fs::path &path) {
  auto value = path.lexically_normal().generic_u8string();
#ifdef _WIN32
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
#endif
  return fs::u8path(value);
}
}  // namespace

/// Validate artifact ownership and detect cycles using a topological traversal.
void validate_build_graph(const Graph &graph) {
  if (graph.targets.empty())
    throw std::runtime_error("build graph has no targets");

  std::map<fs::path, std::vector<fs::path>> edges;
  std::map<fs::path, size_t> incoming;
  std::set<fs::path> outputs;
  // Each normalized output path must have exactly one producer.
  const auto produce = [&](const fs::path &path) {
    if (path.empty() || !path.is_absolute())
      throw std::runtime_error("build artifact must have an absolute path");

    validate_generated_path(path);

    if (!outputs.insert(artifact_key(path)).second)
      throw std::runtime_error("multiple build commands produce " +
                               path.u8string());
  };

  // Record prerequisite-to-output edges and their incoming dependency counts.
  const auto connect = [&](const fs::path &input, const fs::path &output) {
    if (input.empty() || !input.is_absolute())
      throw std::runtime_error("build input must have an absolute path");

    const auto prerequisite = artifact_key(input);
    const auto artifact = artifact_key(output);
    edges[prerequisite].push_back(artifact);
    incoming.try_emplace(prerequisite, 0);
    ++incoming[artifact];
  };

  std::set<std::string> foreign_ids;
  for (const auto &build : graph.project.foreign_builds) {
    if (build.id.empty() || !foreign_ids.insert(build.id).second ||
        build.arguments.empty() || !build.directory.is_absolute() ||
        !std::all_of(build.id.begin(),
                     build.id.end(),
                     [](const unsigned char c) {
                       return (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-' || c == '_';
                     }))
      throw std::runtime_error("duplicate or invalid foreign build action");
    for (const auto &artifact : build.artifacts)
      produce(artifact);
  }

  for (const auto &target : graph.targets) {
    if (target.units.empty() || target.link.empty())
      throw std::runtime_error(
          "build target has no compilation or link commands");

    produce(target.output);
    for (const auto &input : target.link_inputs)
      connect(input, target.output);
    if (!target.import_library.empty()) {
      produce(target.import_library);
      connect(target.output, target.import_library);
    }

    for (const auto dependency : target.dependencies) {
      if (dependency >= graph.targets.size())
        throw std::runtime_error("target dependency index is out of range");
      if (graph.targets[dependency].type == TargetType::Executable)
        throw std::runtime_error("cannot link an executable target");
      connect(graph.targets[dependency].output, target.output);
    }

    for (const auto dependency : target.link_dependencies)
      if (dependency >= graph.targets.size() ||
          graph.targets[dependency].type == TargetType::Executable)
        throw std::runtime_error("invalid linked library target");

    for (const auto &prefix : target.precompiled_headers) {
      if (prefix.creator.arguments.empty() || prefix.headers.empty())
        throw std::runtime_error("PCH step has no compiler command or headers");

      produce(prefix.creator.object);
      connect(prefix.header, prefix.creator.object);
      if (prefix.creator.source != prefix.header)
        connect(prefix.creator.source, prefix.creator.object);
      for (const auto &header : prefix.headers)
        connect(header, prefix.creator.object);

      if (!prefix.companion_object.empty()) {
        produce(prefix.companion_object);
        connect(prefix.creator.object, prefix.companion_object);
        connect(prefix.companion_object, target.output);
      }
    }

    for (const auto &unit : target.units) {
      const auto &[source, object, arguments] = unit;
      if (arguments.empty())
        throw std::runtime_error("build unit has no compiler command");

      produce(object);
      connect(source, object);
      if (const auto *const prefix = pch::for_unit(target, unit))
        connect(prefix->creator.object, object);
      connect(object, target.output);
    }
  }

  // Kahn's algorithm consumes all nodes of a DAG; remaining nodes imply a
  // cycle.
  std::queue<fs::path> ready;
  for (const auto &[path, count] : incoming) {
    if (count == 0)
      ready.push(path);
  }

  size_t visited = 0;
  while (!ready.empty()) {
    const auto path = ready.front();
    ready.pop();
    ++visited;
    for (const auto &dependent : edges[path]) {
      if (--incoming.at(dependent) == 0)
        ready.push(dependent);
    }
  }

  if (visited != incoming.size()) {
    for (const auto &[path, count] : incoming) {
      if (count != 0)
        throw std::runtime_error(
            "build graph contains a dependency cycle involving " +
            path.u8string());
    }
  }

  validate_runtime_dependencies(graph);
}

/// Translate portable settings into exact compiler/linker argument vectors.
Graph make_build_graph(Project project,
                       const Toolchain &toolchain,
                       const BuildBackend &backend,
                       const Options &options) {
  Graph graph;
  graph.project = std::move(project);
  graph.toolchain = toolchain;
  graph.backend = backend;

  const auto specs = resolve_targets(graph.project);
  const auto &cc = toolchain.cc;
  const auto &cxx = toolchain.cxx;
  for (size_t target_index = 0; target_index < specs.size(); ++target_index) {
    const auto &resolved = specs[target_index];
    const auto &spec = resolved.spec;
    const auto &name = spec.name;
    const auto &sources = spec.sources;
    const auto &p = graph.project;
    Target target;
    target.name = name;
    target.local_name = spec.local_name.empty() ? name : spec.local_name;
    target.project_name =
        spec.project_name.empty() ? p.name : spec.project_name;
    target.source_root = spec.source_root.empty() ? p.root : spec.source_root;
    target.additional_files = spec.additional_files;
    target.c_standard =
        spec.c_standard.empty() ? p.c_standard : spec.c_standard;
    target.cxx_standard =
        spec.cxx_standard.empty() ? p.cxx_standard : spec.cxx_standard;
    const auto require_standard = [](std::string &current,
                                     const std::string &required) {
      if (required.empty() || required == "98" || required == "99")
        return;
      if (required != "11" && required != "14" && required != "17" &&
          required != "20" && required != "23")
        throw std::runtime_error(
            "dependency requires an unsupported language standard: " +
            required);
      if (current == "99" || std::stoi(current) < std::stoi(required))
        current = required;
    };
    for (const auto &usage : spec.dependency_usage) {
      require_standard(target.c_standard, usage.c_standard);
      require_standard(target.cxx_standard, usage.cpp_standard);
      if (std::any_of(p.foreign_builds.begin(),
                      p.foreign_builds.end(),
                      [&](const dependencies::ForeignBuild &build) {
                        return build.id == usage.id;
                      }))
        target.foreign_dependencies.push_back(usage.id);
    }
    target.type = spec.type;
    target.is_test = spec.is_test;
    target.build_by_default = spec.build_by_default;
    target.dependencies = resolved.dependencies;
    target.link_dependencies = resolved.link_dependencies;
    target.includes = spec.external ? spec.includes
                                    : merge_includes(p.includes, spec.includes);
    target.defines =
        spec.external ? spec.defines : merge_defines(p.defines, spec.defines);
    target.output = target_output(p, spec, toolchain.msvc);
    target.import_library =
        target_import_library(target.output, spec.type, toolchain.msvc);

    // Per-target objects let an application and tests share a source safely.
    const auto objects =
        graph.project.is_workspace || spec.external
            ? p.build / "obj" / target.project_name / target.local_name
        : graph.project.targets.empty() && target_index == 0
            ? p.build / "obj"
            : p.build / "obj" / ((spec.is_test ? "test-" : "target-") + name);
    for (size_t index = 0; index < sources.size(); ++index) {
      Unit u;
      u.source = sources[index];
      u.object =
          objects / (std::to_string(index) + (toolchain.msvc ? ".obj" : ".o"));
      const bool c = u.source.extension() == ".c";
      u.arguments.push_back(c ? cc : cxx);
      auto &a = u.arguments;
      // Keep the entire compile command in the graph so every generator and the
      // compilation database preserve the same standards, includes and macros.
      if (toolchain.msvc) {
        if (c && (target.c_standard == "99" || target.c_standard == "23"))
          throw std::runtime_error(
              "MSVC C builds currently support std 11 or 17");

        a.insert(a.end(), {"/nologo", "/c", "/showIncludes", "/utf-8"});
        a.emplace_back(p.runtime == "dynamic"
                           ? (options.release ? "/MD" : "/MDd")
                           : (options.release ? "/MT" : "/MTd"));
        if (!c && target.cxx_standard == "11")
          throw std::runtime_error(
              "MSVC-style drivers require C++ std 14 or newer");

        a.push_back(c ? "/std:c" + target.c_standard
                      : "/std:c++" + (target.cxx_standard == "23"
                                          ? std::string("latest")
                                          : target.cxx_standard));
        if (!c)
          a.emplace_back("/EHsc");

        a.emplace_back(p.optimize == "speed"  ? "/O2"
                       : p.optimize == "size" ? "/O1"
                                              : "/Od");
        if (!options.release)
          a.emplace_back("/Z7");

        a.emplace_back(p.warnings == "extra"  ? "/W4"
                       : p.warnings == "none" ? "/W0"
                                              : "/W3");
        for (const auto &inc : target.includes)
          a.push_back("/I" + inc.u8string());

        for (const auto &def : target.defines)
          a.push_back("/D" + def);

        a.push_back(u.source.u8string());
        a.push_back("/Fo" + u.object.u8string());
      } else {
        if (toolchain.microsoft_abi) {
          const auto *const runtime = p.runtime == "dynamic" ? "dll" : "static";
          a.push_back(std::string("-fms-runtime-lib=") + runtime +
                      (options.release ? "" : "_dbg"));
          if (!options.release)
            a.emplace_back("-D_DEBUG");
        }

        a.push_back(std::string("-std=") + (c ? "c" + target.c_standard
                                              : "c++" + target.cxx_standard));
        a.emplace_back(p.optimize == "speed"  ? "-O3"
                       : p.optimize == "size" ? "-Os"
                                              : "-O0");
        if (!options.release)
          a.emplace_back("-g");

        if (p.warnings == "none")
          a.emplace_back("-w");
        else
          a.emplace_back("-Wall");

        if (p.warnings == "extra")
          a.emplace_back("-Wextra");

        for (const auto &inc : target.includes)
          a.push_back("-I" + inc.u8string());

        for (const auto &def : target.defines)
          a.push_back("-D" + def);

#ifndef _WIN32
        if (spec.type != TargetType::Executable)
          a.emplace_back("-fPIC");
#endif
        // Compiler depfiles drive precise header rebuilds in Ninja/GNU Make.
        a.insert(a.end(),
                 {"-MMD",
                  "-MF",
                  u.object.u8string() + ".d",
                  "-c",
                  u.source.u8string(),
                  "-o",
                  u.object.u8string()});
      }

      for (const auto &usage : spec.dependency_usage) {
        const auto &flags = c ? usage.c_options : usage.cpp_options;
        u.arguments.insert(u.arguments.begin() + 1, flags.begin(), flags.end());
      }
      target.units.push_back(std::move(u));
    }

    pch::plan(target, spec, p, toolchain);

    const auto &target_linker = resolved.needs_cpp ? cxx : cc;
    if (spec.type == TargetType::StaticLibrary) {
      std::vector<std::string> candidates;
      if (toolchain.msvc)
        candidates = {
            (fs::u8path(target_linker).parent_path() / "lib.exe").u8string(),
            "llvm-lib",
            "lib"};
      else
        candidates = {
            (fs::u8path(target_linker).parent_path() / "llvm-ar").u8string(),
            "llvm-ar",
            "ar"};
#ifdef __APPLE__
      candidates.insert(candidates.begin(), "/usr/bin/libtool");
#endif

      std::string archiver;
      for (const auto &candidate : candidates) {
        if (const auto program = find_program(candidate); !program.empty()) {
          archiver = program;
          break;
        }
      }
      if (archiver.empty())
        throw std::runtime_error("static target requires an installed "
                                 "ar/llvm-ar or lib/llvm-lib archiver");

      target.link =
          toolchain.msvc
              ? std::vector<std::string>{archiver,
                                         "/NOLOGO",
                                         "/OUT:" + target.output.u8string()}
          : fs::u8path(archiver).filename() == "libtool"
              ? std::vector<std::string>{archiver,
                                         "-static",
                                         "-o",
                                         target.output.u8string()}
              : std::vector<std::string>{archiver,
                                         "rcs",
                                         target.output.u8string()};
      if (fs::u8path(archiver).stem() == "llvm-ar")
        target.link.insert(target.link.begin() + 1, "--rsp-quoting=posix");
    } else {
      target.link = {target_linker};
      if (!toolchain.msvc && toolchain.microsoft_abi) {
        const auto *const runtime = p.runtime == "dynamic" ? "dll" : "static";
        target.link.push_back(std::string("-fms-runtime-lib=") + runtime +
                              (options.release ? "" : "_dbg"));
        // GNU Clang injects release libcmt as a startup default even when the
        // selected object/runtime is Debug or DLL. Microsoft LINK then mixes
        // CRTs. Suppress that driver default and name the selected CRT plus
        // the compatibility library explicitly; retain compiler-rt handling.
        const auto *const crt = p.runtime == "dynamic"
                                    ? (options.release ? "msvcrt" : "msvcrtd")
                                    : (options.release ? "libcmt" : "libcmtd");
        target.link.insert(target.link.end(),
                           {"-nostartfiles",
                            "-Xlinker",
                            std::string("/defaultlib:") + crt,
                            "-Xlinker",
                            "/defaultlib:oldnames"});
      }

      if (toolchain.msvc)
        target.link.insert(target.link.end(),
                           {"/nologo",
                            p.runtime == "dynamic"
                                ? (options.release ? "/MD" : "/MDd")
                                : (options.release ? "/MT" : "/MTd")});
    }
    for (const auto &unit : target.units)
      target.link.push_back(unit.object.u8string());

    for (const auto &prefix : target.precompiled_headers)
      if (!prefix.companion_object.empty())
        target.link.push_back(prefix.companion_object.u8string());

    if (spec.type != TargetType::StaticLibrary) {
      for (const auto dependency : resolved.link_dependencies) {
        const auto output = target_output(graph.project,
                                          specs[dependency].spec,
                                          toolchain.msvc);
        const auto import = target_import_library(output,
                                                  specs[dependency].spec.type,
                                                  toolchain.msvc);
        target.link.push_back((import.empty() ? output : import).u8string());
      }
      if (toolchain.msvc) {
        if (spec.type == TargetType::SharedLibrary)
          target.link.emplace_back("/LD");
        target.link.push_back("/Fe" + target.output.u8string());
        target.link.emplace_back("/link");
        // Native backends decide when linking is needed. LINK's separate
        // timestamp cache can reuse stale archive members during rapid edits.
        target.link.emplace_back("/INCREMENTAL:NO");
        if (!options.release)
          target.link.emplace_back("/DEBUG");
        if (!target.import_library.empty())
          target.link.push_back("/IMPLIB:" + target.import_library.u8string());
      } else {
        if (spec.type == TargetType::SharedLibrary) {
#ifdef __APPLE__
          target.link.insert(target.link.end(),
                             {"-dynamiclib",
                              "-Wl,-install_name,@rpath/" +
                                  target.output.filename().u8string()});
#elif defined(_WIN32)
          target.link.emplace_back("-shared");
#else
          // A loader filename avoids embedding the build directory in ELF
          // DT_NEEDED entries, allowing the existing $ORIGIN paths to work.
          target.link.insert(
              target.link.end(),
              {"-shared",
               "-Wl,-soname," + target.output.filename().u8string()});
#endif
        }
#ifdef _WIN32
        if (!target.import_library.empty()) {
          if (toolchain.microsoft_abi)
            target.link.insert(
                target.link.end(),
                {"-Xlinker", "/implib:" + target.import_library.u8string()});
          else
            target.link.push_back("-Wl,--out-implib," +
                                  target.import_library.u8string());
        }
#elif defined(__APPLE__)
        target.link.insert(
            target.link.end(),
            {"-Wl,-rpath,@loader_path", "-Wl,-rpath,@loader_path/.."});
#else
        target.link.insert(target.link.end(),
                           {"-Wl,-rpath,$ORIGIN", "-Wl,-rpath,$ORIGIN/.."});
#endif
#ifndef _WIN32
        // Member libraries may live beside another project's artifacts. Store
        // loader-relative paths so moving the entire build tree remains valid.
        std::set<fs::path> runtime_directories;
        for (const auto dependency : resolved.link_dependencies) {
          if (specs[dependency].spec.type != TargetType::SharedLibrary)
            continue;

          const auto library =
              target_output(p, specs[dependency].spec, toolchain.msvc);
          if (const auto relative = library.parent_path().lexically_relative(
                  target.output.parent_path());
              runtime_directories.insert(relative).second)
#ifdef __APPLE__
            target.link.push_back("-Wl,-rpath,@loader_path/" +
                                  relative.generic_u8string());
#else
            target.link.push_back("-Wl,-rpath,$ORIGIN/" +
                                  relative.generic_u8string());
#endif
        }
#endif
        target.link.insert(target.link.end(), {"-o", target.output.u8string()});
      }
      // Imported interfaces follow native archives, preserving static link
      // ordering. Archives keep their requirements for the final consumer.
      std::set<std::string> imported;
      const auto consume = [&](const std::vector<DependencyUsage> &usages) {
        for (const auto &usage : usages) {
          if (!imported.insert(usage.id).second)
            continue;
          target.link.insert(target.link.end(),
                             usage.link_options.begin(),
                             usage.link_options.end());
          target.link_inputs.insert(target.link_inputs.end(),
                                    usage.link_inputs.begin(),
                                    usage.link_inputs.end());
          target.runtime_libraries.insert(target.runtime_libraries.end(),
                                          usage.runtime_libraries.begin(),
                                          usage.runtime_libraries.end());
          if (std::any_of(p.foreign_builds.begin(),
                          p.foreign_builds.end(),
                          [&](const dependencies::ForeignBuild &build) {
                            return build.id == usage.id;
                          }) &&
              std::find(target.foreign_dependencies.begin(),
                        target.foreign_dependencies.end(),
                        usage.id) == target.foreign_dependencies.end())
            target.foreign_dependencies.push_back(usage.id);
        }
      };
      consume(spec.dependency_usage);
      for (const auto dependency : resolved.link_dependencies)
        consume(specs[dependency].spec.dependency_usage);
    }

    graph.targets.push_back(std::move(target));
  }

  const auto runnable = std::find_if(
      graph.targets.begin(),
      graph.targets.end(),
      [](const Target &target) {
        return target.type == TargetType::Executable && !target.is_test;
      });
  graph.project.output = runnable == graph.targets.end()
                             ? graph.targets.front().output
                             : runnable->output;
  validate_build_graph(graph);
  return graph;
}
}  // namespace clutch
