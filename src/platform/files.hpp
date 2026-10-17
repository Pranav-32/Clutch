/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Write generated text files only when their content changes, preserving
 * incremental build timestamps.
 */

#pragma once

#include "core/paths.hpp"

#include <string>

namespace clutch
{
    /// Write generated text only when it differs, preserving unchanged output
/// timestamps. Callers validate output ownership before invoking this helper;
/// the writer creates missing parent directories and reports stream failures.
    void write_if_changed(const fs::path& path, const std::string& content);
} // namespace clutch
