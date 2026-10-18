/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Validate target DAGs and compute compile interfaces and static link
 * order.
 */
#include "resolve.hpp"

#include "core/build_graph.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clutch {
namespace {
/// Keep the first occurrence, preserving include-search and definition order.
template <class T>
void append_unique(std::vector<T> &destination, const std::vector<T> &source) {
  for (const auto &value : source)
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end())
      destination.push_back(value);
}
}  // namespace

std::vector<ResolvedTarget> resolve_targets(const Project &project) {
  auto specs = project.targets;
  if (specs.empty()) {
    TargetSpec application;
    application.name = project.name;
    application.sources = project.sources;
    specs.push_back(std::move(application));
  }

  specs.insert(specs.end(), project.tests.begin(), project.tests.end());
  std::map<std::string, size_t> names;
  for (size_t index = 0; index < specs.size(); ++index)
    if (!names.emplace(specs[index].name, index).second)
      throw std::runtime_error("duplicate target name: " + specs[index].name);

  std::vector<ResolvedTarget> result(specs.size());
  std::vector<std::vector<size_t>> exported(specs.size());
  for (size_t index = 0; index < specs.size(); ++index) {
    result[index].spec = specs[index];
    const auto links = [&](const std::vector<std::string> &values,
                           const bool publish) {
      for (const auto &name : values) {
        const auto found = names.find(name);
        if (found == names.end())
          throw std::runtime_error("unknown linked target '" + name + "' in " +
                                   specs[index].name);

        if (specs[found->second].type == TargetType::Executable)
          throw std::runtime_error("cannot link executable target: " + name);

        append_unique(result[index].dependencies, std::vector{found->second});
        if (publish)
          append_unique(exported[index], std::vector{found->second});
      }
    };
    links(specs[index].links, false);
    links(specs[index].public_links, true);
  }

  // Resolve providers first. An in-progress node identifies a real target
  // cycle, including cycles containing only static libraries.
  enum class VisitState {
    New,
    Active,
    Done
  };
  std::vector state(specs.size(), VisitState::New);
  std::function<void(size_t)> visit = [&](const size_t index) {
    if (state[index] == VisitState::Active)
      throw std::runtime_error("target dependency cycle involving " +
                               specs[index].name);

    if (state[index] == VisitState::Done)
      return;

    state[index] = VisitState::Active;
    for (const auto dependency : result[index].dependencies)
      visit(dependency);

    auto &resolved = result[index];
    auto &spec = resolved.spec;
    resolved.needs_cpp = std::any_of(spec.sources.begin(),
                                     spec.sources.end(),
                                     [](const fs::path &source) {
                                       return source.extension() != ".c";
                                     });
    // Public interfaces include only public links, while private compile use
    // receives public requirements of every direct dependency.
    for (const auto dependency : exported[index]) {
      append_unique(spec.public_includes,
                    result[dependency].spec.public_includes);
      append_unique(spec.public_defines,
                    result[dependency].spec.public_defines);
    }
    append_unique(spec.includes, spec.public_includes);
    append_unique(spec.defines, spec.public_defines);
    for (const auto dependency : resolved.dependencies) {
      append_unique(spec.includes, result[dependency].spec.public_includes);
      append_unique(spec.defines, result[dependency].spec.public_defines);
      resolved.needs_cpp = resolved.needs_cpp || result[dependency].needs_cpp;
    }

    // Traverse private dependencies of archives: archives do not absorb their
    // dependencies. Shared libraries expose only their public link interface.
    std::set<size_t> reachable;
    std::function<void(size_t)> collect = [&](const size_t dependency) {
      if (!reachable.insert(dependency).second)
        return;

      const auto &children = specs[dependency].type == TargetType::StaticLibrary
                                 ? result[dependency].dependencies
                                 : exported[dependency];
      for (const auto child : children)
        collect(child);
    };
    for (const auto dependency : resolved.dependencies)
      collect(dependency);

    // A topological link order handles diamonds (B,C before their common D),
    // unlike a naive depth-first traversal or alphabetical library sorting.
    std::map<size_t, size_t> incoming;
    for (const auto dependency : reachable)
      incoming[dependency] = 0;
    for (const auto dependency : reachable)
      for (const auto child : result[dependency].dependencies)
        if (reachable.count(child))
          ++incoming[child];

    std::queue<size_t> ready;
    for (const auto &[dependency, count] : incoming)
      if (count == 0)
        ready.push(dependency);

    while (!ready.empty()) {
      const auto dependency = ready.front();
      ready.pop();
      resolved.link_dependencies.push_back(dependency);
      for (const auto child : result[dependency].dependencies)
        if (reachable.count(child) && --incoming.at(child) == 0)
          ready.push(child);
    }
    state[index] = VisitState::Done;
  };
  for (size_t index = 0; index < specs.size(); ++index)
    visit(index);

  return result;
}

fs::path
target_output(const Project &project, const TargetSpec &spec, const bool msvc) {
  const auto &name = spec.local_name.empty() ? spec.name : spec.local_name;
  const auto directory = spec.project_name.empty()
                             ? project.build
                             : project.build / "members" / spec.project_name;
#ifdef _WIN32
  // Windows treats DOS device names as devices even when an extension is added.
  auto device = name;
  std::transform(device.begin(),
                 device.end(),
                 device.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::toupper(c));
                 });
  constexpr std::array device_names{"CON", "PRN", "AUX", "NUL"};
  bool reserved_device =
      std::find(device_names.begin(), device_names.end(), device) !=
      device_names.end();
  for (const std::string_view prefix : {"COM", "LPT"})
    reserved_device =
        reserved_device || (device.size() == prefix.size() + 1 &&
                            device.compare(0, prefix.size(), prefix) == 0 &&
                            device.back() >= '1' && device.back() <= '9');
  if (reserved_device)
    throw std::runtime_error("target name is a reserved Windows device: " +
                             spec.name);
#else
  if (spec.type == TargetType::Executable && !spec.is_test) {
    // Executables have no suffix on POSIX; protect generated files/directories.
    constexpr std::array reserved{"Makefile",
                                  "obj",
                                  "tests",
                                  "msbuild",
                                  "xcode-obj"};
    if (std::find(reserved.begin(), reserved.end(), name) != reserved.end())
      throw std::runtime_error("target name conflicts with build metadata: " +
                               spec.name);
  }
#endif
  auto output = spec.is_test ? directory / "tests" / name : directory / name;
  if (spec.type == TargetType::Executable) {
#ifdef _WIN32
    output += ".exe";
#endif
    return output;
  }
#ifdef _WIN32
  if (spec.type == TargetType::SharedLibrary)
    return directory / (name + ".dll");

  return directory / (msvc ? name + ".lib" : "lib" + name + ".a");
#else
  (void)msvc;
  if (spec.type == TargetType::StaticLibrary)
    return directory / ("lib" + name + ".a");
#ifdef __APPLE__
  return directory / ("lib" + name + ".dylib");
#else
  return directory / ("lib" + name + ".so");
#endif
#endif
}

fs::path target_import_library(const fs::path &output,
                               const TargetType type,
                               const bool msvc) {
#ifdef _WIN32
  if (type == TargetType::SharedLibrary)
    return output.parent_path() /
           (msvc ? output.stem().string() + ".lib"
                 : "lib" + output.stem().string() + ".dll.a");
#else
  (void)output;
  (void)type;
  (void)msvc;
#endif
  return {};
}

std::vector<size_t> select_targets(const Graph &graph,
                                   const std::vector<std::string> &names,
                                   const bool tests,
                                   const bool runnable,
                                   const std::string &filter) {
  std::vector<size_t> result;
  if (!names.empty()) {
    for (const auto &name : names) {
      const auto found = std::find_if(graph.targets.begin(),
                                      graph.targets.end(),
                                      [&](const Target &target) {
                                        return target.name == name;
                                      });
      if (found == graph.targets.end())
        throw std::runtime_error("unknown target: " + name);

      if ((tests && !found->is_test) ||
          (runnable &&
           (found->type != TargetType::Executable || found->is_test)))
        throw std::runtime_error(
            "selected target is not a runnable application/test: " + name);

      append_unique(
          result,
          std::vector{static_cast<size_t>(found - graph.targets.begin())});
    }
  } else {
    for (size_t index = 0; index < graph.targets.size(); ++index) {
      const auto &target = graph.targets[index];
      if (tests ? target.is_test
                : !target.is_test && target.build_by_default &&
                      (!runnable || target.type == TargetType::Executable))
        result.push_back(index);
    }
  }
  if (tests && !filter.empty())
    result.erase(std::remove_if(result.begin(),
                                result.end(),
                                [&](const size_t index) {
                                  return graph.targets[index].name.find(
                                             filter) == std::string::npos;
                                }),
                 result.end());

  if (runnable && result.size() != 1)
    throw std::runtime_error(
        "run requires one executable; use --target <name>");

  return result;
}
}  // namespace clutch
