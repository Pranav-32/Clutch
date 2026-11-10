/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Resolve optional native tools from PATH and known IDE bundle layouts.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
/// Return whether folder name `left` sorts before `right` when ordering
/// installations newest first. Digit runs compare numerically, so "2022" is
/// newer than "18" and "14.51" is newer than "14.9".
bool newer_version_name(const std::string &left, const std::string &right);

/// Map a Visual Studio installation folder ("2022", "18") to its product
/// major version (17, 18). Unknown or non-numeric folders return -1 and sort
/// after recognized versions.
int visual_studio_major(const std::string &folder);

/// Enumerate executables in one installation without searching other roots.
/// Supports CLion, Visual Studio and Xcode layouts; never runs a discovered
/// tool.
std::vector<std::string> bundle_programs(const fs::path &installation,
                                         const std::string &name);

/// Standalone toolchain and package-manager bin directories searched after IDE
/// bundles: LLVM, MSYS2, Scoop, Chocolatey and WinGet links on Windows;
/// Homebrew on macOS; versioned /usr/lib/llvm-*, /usr/local and Linuxbrew on
/// Linux. Order is deterministic; missing directories are harmless.
std::vector<fs::path> common_tool_directories();

/// Return PATH followed by known IDE installations, then common toolchain
/// directories, in deterministic order.
/// Explicit paths are exact requests: absent paths never fall back to a bundle.
/// Only known tool names are searched; executable spelling is preserved.
std::vector<std::string> tool_candidates(const std::string &name);

/// Resolve one tool using PATH first, then optional IDE-bundle fallback.
/// This establishes executable availability, not runtime or ABI compatibility.
std::string find_tool(const std::string &name);
}  // namespace clutch
