/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Describe supported file-API layouts, query modes and object schemas.
 */
#include "protocol.hpp"

#include "core/version.hpp"

namespace clutch::file_api {
Json schema_version() {
  return {{"major", object_major}, {"minor", object_minor}};
}

Json diagnostic_message(const std::string &message) {
  // Compact serialization with replacement produces valid JSON even when the
  // original exception quotes malformed input. Parse back to a string value.
  return Json::parse(
      Json(message).dump(-1, ' ', false, Json::error_handler_t::replace));
}

Json capabilities() {
  auto kinds = Json::array();
  for (const auto *kind : object_kinds)
    kinds.push_back({{"kind", kind}, {"version", schema_version()}});

  return {{"clutch", {{"version", version}}},
          {"fileApi",
           {{"version", {{"major", layout_version}, {"minor", layout_minor}}},
            {"directory", api_directory},
            {"queryModes",
             {"shared-stateless", "client-stateless", "client-stateful"}},
            {"objectKinds", kinds}}}};
}
}  // namespace clutch::file_api
