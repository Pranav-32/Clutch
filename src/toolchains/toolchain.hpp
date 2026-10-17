/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Discover C and C++ compiler drivers and persist their profile-specific
 * selections.
 */

#pragma once

#include <string>

namespace clutch {
struct Options;
struct Project;

/// Resolved executable paths; an unused language may have an empty driver path.
struct Toolchain {
  std::string cc, cxx;
  bool msvc =
      false;  ///< Selects Microsoft-style flags, including clang-cl drivers.
};

/// Resolve C/C++ compiler drivers and verify compatible driver conventions.
Toolchain resolve_toolchain(const Options &options, const Project &project);
/// Persist compiler selections independently of build-backend selection.
void save_toolchain(const Toolchain &tools, const Project &project);
}  // namespace clutch
