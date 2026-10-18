/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Shared on-disk names used by configuration, generators and workflows.
 */
#pragma once

#include <string>

namespace clutch::layout {
// These names form the project layout contract; keep writers and readers
// aligned.
inline constexpr auto manifest = "clutch.toml";
inline constexpr auto environment = ".clutch";
inline constexpr auto build_prefix = "Clutch-build-";
// The owner marker protects user files from accidental build-directory cleanup.
inline constexpr auto owner_marker = ".clutch-build-owner";
inline constexpr auto configuration = "configuration.toml";
inline constexpr auto backend_cache = "backend.toml";
inline constexpr auto toolchain_cache = "toolchain.toml";
inline constexpr auto compilation_database = "compile_commands.json";
inline constexpr auto build_graph = "build-graph.dot";
// Portable dependency resolution belongs in source control, unlike backend
// caches.
inline constexpr auto lock_file = "clutch.lock";
// Transient, profile-independent mutex beside the portable resolution file.
inline constexpr auto lock_writer = "clutch.lock.write-lock";

/// Keep scaffolded profiles and implicit profile paths under the same policy.
inline std::string default_build_directory(const bool release) {
  return std::string(build_prefix) + (release ? "release" : "debug");
}
}  // namespace clutch::layout
