/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Negotiate object versions and parse bounded, strict client JSON
 * queries.
 */
#include "query.hpp"

#include "paths.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace clutch::file_api {
namespace {
/// Protocol errors are data, so one bad client does not disable other clients.
Json diagnostic(const std::string &code, const std::string &message) {
  return {
      {"error", {{"code", code}, {"message", diagnostic_message(message)}}}};
}

/// Major/minor fields are integer tokens, not floats or coerced strings/bools.
std::uint64_t natural(const Json &value) {
  if (!value.is_number_integer())
    throw std::runtime_error("version must contain nonnegative integers");

  if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)
    throw std::runtime_error("version must contain nonnegative integers");

  return value.get<std::uint64_t>();
}

/// Validate every alternative, selecting the first supported version.
bool supported_version(const Json &value) {
  if (value.is_array()) {
    if (value.empty() || value.size() > maximum_requests)
      throw std::runtime_error(
          "version alternatives must be a nonempty bounded array");

    bool supported = false;
    for (const auto &alternative : value) {
      if (alternative.is_array())
        throw std::runtime_error("nested version alternatives are invalid");

      supported = supported_version(alternative) || supported;
    }

    return supported;
  }

  if (value.is_object()) {
    const auto major = natural(value.at("major"));
    const auto minor = natural(value.value("minor", Json(0)));
    return major == object_major && minor <= object_minor;
  }

  return natural(value) == object_major;
}

Json request_reply(const Json &request, Queries &queries) {
  try {
    if (!request.is_object() || !request.at("kind").is_string())
      throw std::runtime_error("request requires a string kind and a version");

    const auto kind = request.at("kind").get<std::string>();
    const bool supported = supported_version(request.at("version"));
    auto result = Json{{"kind", kind}};
    if (std::find(object_kinds.begin(), object_kinds.end(), kind) ==
        object_kinds.end())
      result.update(diagnostic("unknown-kind", "unsupported object kind"));
    else if (!supported)
      result.update(diagnostic("unsupported-version",
                               "no requested object version is supported"));
    else {
      result["version"] = schema_version();
      queries.kinds.insert(kind);
    }

    if (request.contains("client"))
      result["client"] = request["client"];

    return result;
  } catch (const std::exception &error) {
    return diagnostic("invalid-request", error.what());
  }
}

/// Reject duplicate object keys and excessive depth instead of accepting
/// ambiguous input or recursing over arbitrary client-controlled nesting.
Json parse_query(const std::string &text) {
  std::vector<std::set<std::string>> keys;
  return Json::parse(
      text,
      [&](const int depth, const Json::parse_event_t event, Json &value) {
        if (depth > maximum_json_depth)
          throw std::runtime_error("query JSON nesting limit exceeded");

        if (event == Json::parse_event_t::object_start)
          keys.emplace_back();

        if (event == Json::parse_event_t::key &&
            !keys.back().insert(value.get<std::string>()).second)
          throw std::runtime_error("duplicate query JSON key");

        if (event == Json::parse_event_t::object_end)
          keys.pop_back();

        return true;
      });
}

/// Echo each client's request ordering/metadata and isolate query-level errors.
Json stateful(const fs::path &path, Queries &queries) {
  try {
    const auto query = parse_query(read_query(path));
    if (!query.is_object() || !query.at("requests").is_array() ||
        query["requests"].size() > maximum_requests)
      throw std::runtime_error("query requires a requests array of at most " +
                               std::to_string(maximum_requests) + " entries");

    auto responses = Json::array();
    for (const auto &request : query["requests"])
      responses.push_back(request_reply(request, queries));

    Json result{{"requests", query["requests"]}, {"responses", responses}};
    if (query.contains("client"))
      result["client"] = query["client"];

    return result;
  } catch (const std::exception &error) {
    return diagnostic("invalid-query", error.what());
  }
}

/// Stateless requests have empty contents and filenames such as codemodel-v1.
Json stateless(const fs::path &path, Queries &queries) {
  const auto name = path.filename().u8string();
  if (const auto separator = name.rfind("-v"); separator != std::string::npos) {
    const auto kind = name.substr(0, separator);
    const auto version = name.substr(separator + 2);
    if (version.empty() || version.size() > maximum_version_digits ||
        (version.size() > 1 && version.front() == '0') ||
        version.find_first_not_of("0123456789") != std::string::npos)
      return diagnostic("invalid-request", "invalid stateless version suffix");

    try {
      if (!read_query(path).empty())
        return diagnostic("invalid-query",
                          "stateless query files must be empty");

      return request_reply({{"kind", kind}, {"version", std::stoull(version)}},
                           queries);
    } catch (const std::exception &error) {
      return diagnostic("invalid-query", error.what());
    }
  }

  return diagnostic("unknown-query", "unrecognized query filename");
}
}  // namespace

Queries read_queries(const fs::path &build) {
  Queries queries;
  size_t files = 0;
  std::uintmax_t bytes = 0;
  // Bound the whole batch as well as each query, so many independent clients
  // cannot multiply the per-file resource limits without bound.
  const auto account = [&](const fs::path &path) {
    if (++files > maximum_entries)
      throw std::runtime_error("file API query batch exceeds 256 files");

    if (fs::is_regular_file(build / path)) {
      bytes += std::min(fs::file_size(build / path),
                        static_cast<std::uintmax_t>(maximum_query_bytes));
      if (bytes > maximum_total_query_bytes)
        throw std::runtime_error("file API query batch exceeds eight MiB");
    }
  };
  const auto root = fs::u8path(api_directory) / "query";
  for (const auto &path : entries(build, root)) {
    const auto name = path.filename().u8string();
    if (fs::is_directory(build / path) && name.find("client-") == 0 &&
        name != "client-") {
      auto client = Json::object();
      for (const auto &child : entries(build, path)) {
        account(child);
        const auto filename = child.filename().u8string();
        client[filename] = filename == "query.json"
                               ? stateful(build / child, queries)
                               : stateless(build / child, queries);
      }

      queries.reply[name] = std::move(client);
    } else {
      account(path);
      queries.reply[name] = stateless(build / path, queries);
    }
  }

  return queries;
}
}  // namespace clutch::file_api
