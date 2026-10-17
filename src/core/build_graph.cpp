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

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace clutch {
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

    if (!outputs.insert(path.lexically_normal()).second)
      throw std::runtime_error("multiple build commands produce " +
                               path.string());
  };

  // Record prerequisite-to-output edges and their incoming dependency counts.
  const auto connect = [&](const fs::path &input, const fs::path &output) {
    if (input.empty() || !input.is_absolute())
      throw std::runtime_error("build input must have an absolute path");

    const auto prerequisite = input.lexically_normal();
    const auto artifact = output.lexically_normal();
    edges[prerequisite].push_back(artifact);
    incoming.try_emplace(prerequisite, 0);
    ++incoming[artifact];
  };

  for (const auto &target : graph.targets) {
    if (target.units.empty() || target.link.empty())
      throw std::runtime_error(
          "build target has no compilation or link commands");

    produce(target.output);
    for (const auto &[source, object, arguments] : target.units) {
      if (arguments.empty())
        throw std::runtime_error("build unit has no compiler command");

      produce(object);
      connect(source, object);
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
            path.string());
    }
  }
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

  const auto &p = graph.project;
  const auto &cc = toolchain.cc;
  const auto &cxx = toolchain.cxx;
  // Index zero is always the application; later indices identify test targets.
  std::vector<Project::Test> specs{{p.name, p.sources}};
  specs.insert(specs.end(), p.tests.begin(), p.tests.end());

  for (size_t target_index = 0; target_index < specs.size(); ++target_index) {
    const auto &[name, sources] = specs[target_index];
    Target target;
    target.name = name;
    target.output = target_index == 0 ? p.output : p.build / "tests" / name;
#ifdef _WIN32
    if (target_index)
      target.output += ".exe";
#endif

    // Per-target objects let an application and tests share a source safely.
    const auto objects = target_index == 0
                           ? p.build / "obj"
                           : p.build / "obj" / ("test-" + name);
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
        if (c && (p.c_standard == "99" || p.c_standard == "23"))
          throw std::runtime_error(
              "MSVC C builds currently support std 11 or 17");

        a.insert(a.end(), {"/nologo", "/c", "/showIncludes", "/utf-8", "/MT"});
        a.push_back(c
                      ? "/std:c" + p.c_standard
                      : "/std:c++" + (p.cxx_standard == "23"
                                        ? std::string("latest")
                                        : p.cxx_standard));
        if (!c)
          a.emplace_back("/EHsc");

        a.emplace_back(p.optimize == "speed"
                         ? "/O2"
                         : p.optimize == "size"
                         ? "/O1"
                         : "/Od");
        if (!options.release)
          a.emplace_back("/Z7");

        a.emplace_back(p.warnings == "extra"
                         ? "/W4"
                         : p.warnings == "none"
                         ? "/W0"
                         : "/W3");
        for (const auto &inc : p.includes)
          a.push_back("/I" + inc.string());

        for (const auto &def : p.defines)
          a.push_back("/D" + def);

        a.push_back(u.source.string());
        a.push_back("/Fo" + u.object.string());
      } else {
        a.push_back(std::string("-std=") +
                    (c ? "c" + p.c_standard : "c++" + p.cxx_standard));
        a.emplace_back(p.optimize == "speed"
                         ? "-O3"
                         : p.optimize == "size"
                         ? "-Os"
                         : "-O0");
        if (!options.release)
          a.emplace_back("-g");

        if (p.warnings == "none")
          a.emplace_back("-w");
        else
          a.emplace_back("-Wall");

        if (p.warnings == "extra")
          a.emplace_back("-Wextra");

        for (const auto &inc : p.includes)
          a.push_back("-I" + inc.string());

        for (const auto &def : p.defines)
          a.push_back("-D" + def);

        // Compiler depfiles drive precise header rebuilds in Ninja/GNU Make.
        a.insert(a.end(),
                 {"-MMD",
                  "-MF",
                  u.object.string() + ".d",
                  "-c",
                  u.source.string(),
                  "-o",
                  u.object.string()});
      }

      target.units.push_back(std::move(u));
    }

    // Link mixed targets through the C++ driver to include its runtime
    // libraries.
    const bool needs_cxx =
        std::any_of(sources.begin(),
                    sources.end(),
                    [](const fs::path &s) {
                      return s.extension() != ".c";
                    });
    const auto &target_linker = needs_cxx ? cxx : cc;
    target.link = {target_linker};
    if (toolchain.msvc)
      target.link.insert(target.link.end(), {"/nologo", "/MT"});

    for (const auto &unit : target.units)
      target.link.push_back(unit.object.string());

    if (toolchain.msvc) {
      target.link.push_back("/Fe" + target.output.string());
      if (!options.release)
        target.link.insert(target.link.end(), {"/link", "/DEBUG"});
    } else
      target.link.insert(target.link.end(), {"-o", target.output.string()});

    graph.targets.push_back(std::move(target));
  }

  validate_build_graph(graph);
  return graph;
}
} // namespace clutch