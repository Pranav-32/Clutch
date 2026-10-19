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

#include "terminal.hpp"
#include "platform/environment.hpp"

#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace clutch {
namespace {
// Bold green emphasizes build status; reset prevents color leaking into later
// output.
constexpr auto status_color = "\033[1;32m";
constexpr auto reset_color = "\033[0m";
}  // namespace

/// Emit a concise status line, respecting terminal capabilities and NO_COLOR.
void status(const std::string &action, const std::string &message) {
#ifdef _WIN32
  bool color = _isatty(_fileno(stdout)) != 0;
  if (color) {
    DWORD mode;
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    color = GetConsoleMode(h, &mode) &&
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#else
  bool color = isatty(STDOUT_FILENO);
#endif
  // Redirected logs remain plain text; an explicit NO_COLOR disables ANSI too.
  color = color && !environment_value("NO_COLOR");
  std::cout << (color ? status_color : "") << action
            << (color ? reset_color : "") << " " << message << '\n';
}
}  // namespace clutch