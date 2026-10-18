/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Export public code-model objects from the resolved build graph.
 */
#pragma once

#include "core/paths.hpp"
#include "protocol.hpp"

#include <map>
#include <set>
#include <string>

namespace clutch {
struct Graph;
struct Options;
struct Project;
}  // namespace clutch

namespace clutch::file_api {
/// Fingerprint file bytes for staleness detection, not cryptographic identity.
Json file_signature(const fs::path &path);
/// Complete manifest/lock snapshot shared by the reply index and inputs object.
Json configuration_inputs(const Project &project);
/// Make immutable reply objects; map keys are internal, generation-local names.
std::map<std::string, Json>
make_objects(const Graph &graph,
             const Options &options,
             const fs::path &executable,
             const std::string &generation_directory,
             const std::set<std::string> &kinds);
}  // namespace clutch::file_api
