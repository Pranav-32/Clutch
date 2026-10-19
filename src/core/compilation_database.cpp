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
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

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

  std::set<fs::path> imported;
  // Upstream private flags belong only to its own records. Merge the original
  // arguments/command records with their original working-directory frames.
  for (const auto &build : graph.project.foreign_builds) {
    if (build.compilation_database.empty() ||
        !fs::exists(build.compilation_database) ||
        !imported.insert(build.compilation_database).second)
      continue;
    constexpr auto maximum_database_bytes = 64U * 1024U * 1024U;
    if (!fs::is_regular_file(build.compilation_database) ||
        fs::file_size(build.compilation_database) > maximum_database_bytes)
      throw std::runtime_error(
          "invalid or oversized foreign compilation database");
    std::ifstream stream(build.compilation_database, std::ios::binary);
    const auto records = nlohmann::json::parse(stream);
    if (!records.is_array())
      throw std::runtime_error("foreign compilation database must be an array");
    for (const auto &record : records) {
      const auto file = fs::u8path(record.at("file").get<std::string>());
      const auto probe = build.directory.parent_path() / "probe";
      if (file.parent_path() == probe)
        continue;
      if (entries++)
        database << ",\n";
      database << "  " << record.dump();
    }
  }
  database << "\n]\n";
  write_if_changed(graph.project.build / layout::compilation_database,
                   database.str());
}
}  // namespace clutch
