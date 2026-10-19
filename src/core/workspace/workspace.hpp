/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Compose member projects into one native target graph.
 */
#pragma once
#include "core/project.hpp"
#include <toml.hpp>

namespace clutch {
/// Parse one manifest without recursively discovering workspaces or writing
/// files. A shared build directory scopes imported/member settings to the
/// containing graph instead of validating an unused child profile directory.
Project load_project_manifest(const Options &options,
                              const toml::table &document,
                              bool virtual_workspace = false,
                              const fs::path &shared_build = {});
/// Enumerate explicit physical member roots; reject duplicates and escaping
/// paths.
std::vector<fs::path> workspace_member_roots(const fs::path &root,
                                             const toml::table &document);
}  // namespace clutch
