/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Copy environment values using the native Windows Unicode interface.
 */
#ifdef _MSC_VER
// The CRT opt-out requires this reserved spelling; values are copied at once.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "environment.hpp"

#include "core/paths.hpp"

#include <cstdlib>

namespace clutch {
std::optional<std::string> environment_value(const char *const name) {
#ifdef _WIN32
  // All callers use ASCII environment keys. Values may contain arbitrary
  // Unicode compiler/install paths and must not pass through the ANSI CRT.
  const std::string key(name);
  const std::wstring wide_key(key.begin(), key.end());
  if (const auto *value = _wgetenv(wide_key.c_str()))
    return fs::path(value).u8string();
#else
  if (const auto *value = std::getenv(name))
    return std::string(value);
#endif
  return std::nullopt;
}
}  // namespace clutch