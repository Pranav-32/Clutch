/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Encode process and response arguments without losing Unicode paths.
 */
#include "arguments.hpp"

#include "core/encoding_constants.hpp"

#include <stdexcept>
#include <string_view>

namespace clutch {
namespace {
/// Microsoft CRT parsing doubles slashes before quotes and the closing quote.
std::string windows_argument(const std::string_view value) {
  std::string result = "\"";
  size_t slashes = 0;
  for (const char character : value) {
    if (character == '\\') {
      ++slashes;
      continue;
    }

    result.append(character == '"' ? slashes * 2U + 1U : slashes, '\\');
    slashes = 0;
    result += character;
  }

  result.append(slashes * 2U, '\\');
  return result + '"';
}
}  // namespace

std::string command_line(const std::vector<std::string> &arguments) {
  std::string result;
  for (const auto &argument : arguments) {
    if (!result.empty())
      result += ' ';
#ifdef _WIN32
    result += windows_argument(argument);
#else
    result += '\'';
    for (const char character : argument) {
      if (character == '\'')
        result += "'\\''";
      else
        result += character;
    }

    result += '\'';
#endif
  }

  return result;
}

std::string response_file(const std::vector<std::string> &command,
                          const bool msvc) {
  // MSVC otherwise interprets response bytes through its active ANSI page.
  std::string result = msvc ? encoding::utf8_bom : "";
  for (size_t index = 1; index < command.size(); ++index) {
    const auto &argument = command[index];
    if (argument.find_first_of("\r\n") != std::string::npos ||
        argument.find('\0') != std::string::npos)
      throw std::runtime_error(
          "newlines and NUL bytes are not supported in response arguments");

    if (msvc) {
      result += windows_argument(argument);
    } else {
      // GNU/LLVM response grammars use backslash escapes inside double quotes;
      // unlike shell recipes, no shell expands dollar signs in these files.
      result += '"';
      for (const char character : argument) {
        if (character == '\\' || character == '"')
          result += '\\';

        result += character;
      }

      result += '"';
    }

    result += ' ';
  }

  return result;
}
}  // namespace clutch
