/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Serialize exact translation-unit arguments as a build-local IDE
 * compilation database.
 */

#include "compilation_database.hpp"

#include "core/build_graph.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/json.hpp"

#include <sstream>
#include <string>

namespace clutch {
/// Emit one database record per application/test unit using exact graph
/// arguments.
void write_compilation_database(const Graph &graph) {
  std::ostringstream database;
  database << "[\n";
  size_t entries = 0;

  for (const auto &target : graph.targets) {
    for (const auto &[source, object, arguments] : target.units) {
      if (entries++)
        database << ",\n";

      // An arguments array avoids shell parsing differences in IDE consumers.
      // The directory frame matches the working directory used by native
      // builds.
      database << "  {\"directory\": "
               << json_string(graph.project.build.u8string())
               << ", \"file\": " << json_string(source.u8string())
               << ", \"output\": " << json_string(object.u8string())
               << ", \"arguments\": [";
      for (size_t i = 0; i < arguments.size(); ++i) {
        if (i)
          database << ", ";

        database << json_string(arguments[i]);
      }

      database << "]}";
    }
  }

  database << "\n]\n";
  write_if_changed(graph.project.build / layout::compilation_database,
                   database.str());
}
}  // namespace clutch
