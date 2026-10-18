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
#include "core/file_api/file_api.hpp"
#include "core/graph_visualization.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/workspace/lock.hpp"
#include "toolchains/toolchain.hpp"

#include <stdexcept>
#include <utility>

namespace clutch {
/// Generate configuration outputs only; no compiler or build executor is run.
Graph configure(const Options &options, const fs::path &executable) {
  file_api::Session api(options);
  try {
    // Validate the manifest and destination before resolving tools or writing
    // files.
    auto project = load_project(options);
    validate_build_directory(project.root, project.build, false);

    // Claim only after validating source/include containment. API queries may
    // already exist in a fresh build directory; they are preserved for clients.
    prepare_build_directory(project);
    api.begin(project.build);

    // Normalize the generator before choosing its compatible compiler defaults.
    const auto backend = resolve_backend(options, project);
    project.generator = backend.name;
    const auto toolchain = resolve_toolchain(options, project);

    // Build and validate one graph shared by generators, IDEs and saved state.
    auto graph =
        make_build_graph(std::move(project), toolchain, backend, options);
    validate_inputs(graph.project.inputs);
    validate_lock_file(graph, options.locked);

    // Serialize native rules only after the output directory has an owner
    // marker.
    generate_backend_files(graph, options, executable);

    // Publish IDE/inspection data and record reusable tool selections. Save the
    // complete graph last so earlier generation failures cannot publish new
    // state.
    write_compilation_database(graph);
    write_graph_visualization(graph);
    update_lock_file(graph, options.locked);
    validate_inputs(graph.project.inputs);
    save_toolchain(toolchain, graph.project);
    save_backend(backend, graph.project);
    save_configuration(graph, options);
    api.publish(graph, executable);

    return graph;
  } catch (const std::exception &error) {
    // A failing compiler probe or generator must not publish a success model.
    // Preserve the original diagnostic if recording the API failure also fails.
    try {
      api.fail(error.what());
    } catch (const std::exception &api_error) {
      throw std::runtime_error(std::string(error.what()) +
                               "; file API: " + api_error.what());
    }

    throw;
  }
}
}  // namespace clutch
