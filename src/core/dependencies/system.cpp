/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Translate explicit installed library contracts for the selected
 * driver.
 */
#include "system.hpp"
#include "toolchains/toolchain.hpp"
#include <stdexcept>

namespace clutch::dependencies {
NativeInterface inspect_system(const ForeignRequest &request,
                               const Toolchain &tools) {
  const auto &r = request.requirement;
  if (r.libraries.empty() && r.include_dirs.empty())
    throw std::runtime_error(
        "system dependency '" + r.alias +
        "' needs target/targets, or explicit libraries/include_dirs");
  if (!r.version.empty() || !r.components.empty() || !r.cmake_package.empty() ||
      !r.cmake_options.empty())
    throw std::runtime_error(
        "explicit system interfaces cannot use CMake discovery/version fields; "
        "select target/targets to discover installed package metadata");

  NativeInterface result;
  result.usage.needs_cpp = r.link_language == "cpp";
  std::string selection = request.identity + ":system:";
  for (const auto &library : r.libraries) {
    selection += std::to_string(library.size()) + ':' + library;
    if (const auto path = fs::u8path(library); path.is_absolute()) {
      result.usage.link_inputs.push_back(path);
      result.usage.link_options.push_back(path.u8string());
    } else {
      // These are data-only linker names, not switches or shell fragments.
      if (library.empty() || library.front() == '-' || library.front() == '@' ||
          library.find_first_of("/\\:; \t\r\n") != library.npos)
        throw std::runtime_error("invalid system library name: " + library);
      result.usage.link_options.push_back(
          tools.msvc ? (path.extension() == ".lib" ? library : library + ".lib")
                     : "-l" + library);
    }
  }
  result.usage.id = source_key(selection);
  return result;
}
}  // namespace clutch::dependencies
