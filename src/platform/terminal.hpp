/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Print concise build status messages with terminal-aware ANSI colors.
 */

#pragma once

#include <string>

namespace clutch {
/// Print a build status line; use ANSI colors only on supported interactive
/// terminals.
void status(const std::string &action, const std::string &message);
} // namespace clutch