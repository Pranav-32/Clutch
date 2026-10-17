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
#include "core/encoding_constants.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"

#include <sstream>
#include <string>

namespace clutch {
namespace {
/// Quote a JSON string, including control bytes, without shell-escaping
/// arguments.
std::string json(const std::string &value) {
  std::string result = "\"";
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < encoding::first_printable_ascii) {
      constexpr auto hex = "0123456789abcdef";
      const auto byte = static_cast<unsigned int>(c);
      result += "\\u00";
      result += hex[byte >> encoding::bits_per_nibble];
      result += hex[byte & encoding::low_nibble_mask];
    } else
      result += static_cast<char>(c);
  }

  return result + '"';
}
} // namespace

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
      database << "  {\"directory\": " << json(graph.project.build.string())
          << ", \"file\": " << json(source.string())
          << ", \"output\": " << json(object.string())
          << ", \"arguments\": [";
      for (size_t i = 0; i < arguments.size(); ++i) {
        if (i)
          database << ", ";

        database << json(arguments[i]);
      }

      database << "]}";
    }
  }

  database << "\n]\n";
  write_if_changed(graph.project.build / layout::compilation_database,
                   database.str());
}
} // namespace clutch