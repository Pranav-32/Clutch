/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Coordinate manifest loading, tool resolution, graph construction and
 * output generation.
 */

#include "configure.hpp"

#include "backends/backend.hpp"
#include "core/build_directory.hpp"
#include "core/compilation_database.hpp"
#include "core/configuration_store.hpp"
#include "core/graph_visualization.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "toolchains/toolchain.hpp"

#include <utility>

namespace clutch {
/// Generate configuration outputs only; no compiler or build executor is run.
Graph configure(const Options &options, const fs::path &executable) {
  // Validate the manifest and destination before resolving tools or writing
  // files.
  auto project = load_project(options);
  validate_build_directory(project.root, project.build, false);

  // Normalize the generator before choosing its compatible compiler defaults.
  const auto backend = resolve_backend(options, project);
  project.generator = backend.name;
  const auto toolchain = resolve_toolchain(options, project);

  // Build and validate one graph shared by generators, IDEs and saved state.
  auto graph =
      make_build_graph(std::move(project), toolchain, backend, options);
  prepare_build_directory(graph.project);

  // Serialize native rules only after the output directory has an owner marker.
  generate_backend_files(graph, options, executable);

  // Publish IDE/inspection data and record reusable tool selections. Save the
  // complete graph last so earlier generation failures cannot publish new
  // state.
  write_compilation_database(graph);
  write_graph_visualization(graph);
  save_toolchain(toolchain, graph.project);
  save_backend(backend, graph.project);
  save_configuration(graph, options);

  return graph;
}
} // namespace clutch