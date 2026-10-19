/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Represent client requests and isolated query diagnostics.
 */
#pragma once

#include "core/paths.hpp"
#include "protocol.hpp"

#include <set>
#include <string>

namespace clutch::file_api {
// MSVC's std::set move can allocate its sentinel. This aggregate makes no
// noexcept move promise; allocation failure reaches configuration's handlers.
// Keep the implicit value operations rather than imposing a false guarantee.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct Queries {
  Json reply = Json::object();
  std::set<std::string> kinds;
};

/// Snapshot shared/client requests. Invalid requests become reply diagnostics;
/// unsafe filesystem layouts throw before any reply is written.
Queries read_queries(const fs::path &build);
}  // namespace clutch::file_api
