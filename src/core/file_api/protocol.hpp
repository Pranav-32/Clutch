/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Public file-API versions, resource limits and supported object kinds.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace clutch::file_api {
using Json = nlohmann::json;

// Layout and object schemas evolve independently of Clutch's release version.
inline constexpr auto layout_version = 1;
inline constexpr auto layout_minor = 0;
inline constexpr auto object_major = 1;
inline constexpr auto object_minor = 0;
inline constexpr auto api_directory = ".clutch/api/v1";
inline constexpr std::array object_kinds{"codemodel", "toolchains", "inputs"};
// Bound client-controlled allocation, recursion and directory enumeration.
inline constexpr auto maximum_query_bytes =
    1024U * 1024U;  // One MiB per query.
inline constexpr auto maximum_json_depth = 64;
inline constexpr auto maximum_entries = 256U;
inline constexpr auto maximum_requests = 128U;
inline constexpr auto maximum_total_query_bytes = 8U * maximum_query_bytes;
// Reply naming is a public protocol convention; avoid duplicated prefix widths.
inline constexpr std::string_view index_prefix = "index-";
inline constexpr std::string_view index_suffix = ".json";
inline constexpr std::string_view generation_prefix = "generation-";
// Human-readable JSON uses two spaces; whitespace has no protocol meaning.
inline constexpr auto json_indent = 2;
// Unsigned 64-bit values require up to 20 decimal digits, including UINT64_MAX.
inline constexpr auto maximum_version_digits =
    std::numeric_limits<std::uint64_t>::digits10 + 1;
// Fixed-width decimal indices sort in publication order, even across restarts.
inline constexpr auto generation_digits = maximum_version_digits;

/// Construct the current schema version without duplicating magic literals.
Json schema_version();
/// Parser/filesystem exceptions can contain invalid input bytes. Replace those
/// only in diagnostic text; public metadata serialization remains strict UTF-8.
Json diagnostic_message(const std::string &message);
/// Advertise the public protocol without loading a project or discovering
/// tools.
Json capabilities();
}  // namespace clutch::file_api
