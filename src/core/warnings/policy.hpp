/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Portable warning names with explicit compiler-specific mappings.
 */
#pragma once

#include <string>
#include <vector>

namespace clutch {
/// Driver spelling and diagnostic family are separate for clang-cl.
enum class WarningDriver {
  Gnu,
  Microsoft,
  ClangCl
};

/// Reject unknown/duplicate names and conflicting baseline selections.
void validate_warning_names(const std::vector<std::string> &names);
/// Translate a preset or ordered custom list; unsupported equivalents are
/// errors.
std::vector<std::string> warning_flags(const std::string &preset,
                                       const std::vector<std::string> &names,
                                       WarningDriver driver,
                                       bool lifetime);
}  // namespace clutch
