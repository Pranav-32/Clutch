/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Resolve and copy local runtime libraries without process-global path
 * changes.
 */
#include "runtime.hpp"

#include "core/build_graph.hpp"
#include "core/options.hpp"

#include <functional>
#include <set>
#include <stdexcept>

namespace clutch {
std::vector<std::pair<size_t, fs::path>> runtime_copies(const Graph &graph,
                                                        const size_t consumer) {
  std::vector<std::pair<size_t, fs::path>> result;
#ifdef _WIN32
  if (graph.targets.at(consumer).type != TargetType::Executable)
    return result;

  const auto directory = graph.targets[consumer].output.parent_path();
  std::set<size_t> visited;
  std::function<void(size_t)> visit = [&](const size_t index) {
    if (!visited.insert(index).second)
      return;

    const auto &target = graph.targets.at(index);
    if (target.type == TargetType::SharedLibrary &&
        target.output.parent_path() != directory)
      result.emplace_back(index, directory / target.output.filename());

    for (const auto dependency : target.dependencies)
      visit(dependency);
  };
  visit(consumer);
#else
  (void)graph;
  (void)consumer;
#endif
  return result;
}

std::vector<std::string> runtime_copy_command(const Graph &graph,
                                              const Options &options,
                                              const fs::path &executable,
                                              const size_t consumer,
                                              const size_t library) {
  std::vector<std::string> result{executable.string(),
                                  "__copy-runtime",
                                  "--project",
                                  graph.project.root.string(),
                                  "--target",
                                  graph.targets.at(consumer).name,
                                  "--library",
                                  graph.targets.at(library).name};
  if (options.release)
    result.emplace_back("--release");
  return result;
}

void copy_runtime_library(const Graph &graph,
                          const size_t consumer,
                          const std::string &library) {
  for (const auto &[index, destination] : runtime_copies(graph, consumer)) {
    if (graph.targets[index].name != library)
      continue;

    if (fs::is_symlink(destination) ||
        fs::canonical(destination.parent_path()) != destination.parent_path())
      throw std::runtime_error(
          "refusing redirected runtime-library destination");

    fs::copy_file(graph.targets[index].output,
                  destination,
                  fs::copy_options::update_existing);
    return;
  }
  throw std::runtime_error("not a staged runtime dependency: " + library);
}
}  // namespace clutch
