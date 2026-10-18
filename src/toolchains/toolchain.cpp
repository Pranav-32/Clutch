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

#ifdef _MSC_VER
// Microsoft's CRT opt-out requires this reserved macro spelling.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "toolchain.hpp"

#include "backends/backend.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include <toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace clutch {
namespace {
/// Apply CLI, environment, cache and discovery precedence; reject broken
/// overrides.
std::string choose(const std::string &explicit_name,
                   const char *const env,
                   const std::string &cached,
                   const std::vector<std::string> &defaults) {
  std::string requested = explicit_name;
  if (const auto *environment = std::getenv(env);
    requested.empty() && environment)
    requested = environment;

  if (requested.empty())
    requested = cached;

  if (!requested.empty()) {
    if (const auto resolved = find_program(requested); !resolved.empty())
      return resolved;

    throw std::runtime_error("cannot find " + requested + "; override " + env +
                             " or pass its command-line option");
  }

  for (const auto &candidate : defaults) {
    if (const auto resolved = find_program(candidate); !resolved.empty())
      return resolved;
  }

  throw std::runtime_error(std::string("tool not found; set ") + env +
                           " or the corresponding command-line option");
}
} // namespace

/// Resolve only languages present in the application or tests, then check
/// drivers.
Toolchain resolve_toolchain(const Options &options, const Project &project) {
  const auto cache_path = project.build / layout::toolchain_cache;
  toml::table cache;
  if (fs::exists(cache_path))
    cache = toml::parse_file(cache_path.string());

  // A C application can still declare a C++ test, so inspect every target's
  // inputs.
  auto sources = project.sources;
  for (const auto &test : project.tests)
    sources.insert(sources.end(), test.sources.begin(), test.sources.end());

  const bool cpp =
      std::any_of(sources.begin(),
                  sources.end(),
                  [](const fs::path &p) {
                    return p.extension() != ".c";
                  });
  const bool c =
      std::any_of(sources.begin(),
                  sources.end(),
                  [](const fs::path &p) {
                    return p.extension() == ".c";
                  });
  Toolchain tools;
  const auto backend = normalize_backend_name(project.generator);
  // Native IDE formats constrain defaults; overrides still undergo driver
  // checks.
  const std::vector<std::string> cxx_defaults =
      backend == "visual-studio" || backend == "nmake" || backend == "jom"
        ? std::vector<std::string>{"cl", "clang-cl"}
        : backend == "xcode"
        ? std::vector<std::string>{"/usr/bin/clang++"}
        : std::vector<std::string>{"clang++", "g++", "cl"};
  const std::vector<std::string> c_defaults =
      backend == "visual-studio" || backend == "nmake" || backend == "jom"
        ? std::vector<std::string>{"cl", "clang-cl"}
        : backend == "xcode"
        ? std::vector<std::string>{"/usr/bin/clang"}
        : std::vector<std::string>{"clang", "gcc", "cl"};
  if (cpp)
    tools.cxx = choose(options.cxx,
                       "CXX",
                       cache["cxx"].value_or(std::string{}),
                       cxx_defaults);

  if (c)
    tools.cc = choose(options.cc,
                      "CC",
                      cache["cc"].value_or(std::string{}),
                      c_defaults);

  // Driver syntax comes from the selected executable, not the host OS alone.
  const auto msvc = [](const std::string &compiler) {
    auto name = fs::path(compiler).stem().string();
    std::transform(name.begin(),
                   name.end(),
                   name.begin(),
                   [](const unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    return name == "cl" || name == "clang-cl";
  };

  tools.msvc = msvc(cpp ? tools.cxx : tools.cc);
  if (c && cpp && msvc(tools.cc) != tools.msvc)
    throw std::runtime_error(
        "C and C++ compilers must use the same driver family");

  if (cpp && tools.msvc && project.cxx_standard == "11")
    throw std::runtime_error("MSVC-style drivers require C++ std 14 or newer");

  return tools;
}

/// Retain resolved executables for the next configuration of this profile.
void save_toolchain(const Toolchain &tools, const Project &project) {
  const toml::table cache{{"cc", tools.cc}, {"cxx", tools.cxx}};
  std::ostringstream text;
  text << cache << '\n';
  write_if_changed(project.build / layout::toolchain_cache, text.str());
}
} // namespace clutch