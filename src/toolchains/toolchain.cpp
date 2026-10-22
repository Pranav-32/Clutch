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

#include "toolchain.hpp"
#include "platform/capture.hpp"

#include "backends/backend.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/environment.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include <toml.hpp>

#include <algorithm>
#include <cctype>
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
  if (const auto environment = environment_value(env);
      requested.empty() && environment)
    requested = *environment;

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
}  // namespace

/// Resolve only languages present in the application or tests, then check
/// drivers.
Toolchain resolve_toolchain(const Options &options, const Project &project) {
  const auto cache_path = project.build / layout::toolchain_cache;
  toml::table cache;
  if (fs::exists(cache_path))
    cache = toml::parse_file(cache_path.u8string());

  // A C application can still declare a C++ test, so inspect every target's
  // inputs.
  auto sources = project.sources;
  for (const auto &test : project.tests)
    sources.insert(sources.end(), test.sources.begin(), test.sources.end());

  const bool cpp =
      !project.foreign_requests.empty() ||
      std::any_of(sources.begin(), sources.end(), [](const fs::path &p) {
        return p.extension() != ".c";
      });
  const bool c =
      !project.foreign_requests.empty() ||
      std::any_of(sources.begin(), sources.end(), [](const fs::path &p) {
        return p.extension() == ".c";
      });
  Toolchain tools;
  const auto backend = normalize_backend_name(project.generator);
  // Native IDE formats constrain defaults; overrides still undergo driver
  // checks.
  const std::vector<std::string> cxx_defaults =
      backend == "visual-studio" || backend == "nmake" || backend == "jom"
          ? std::vector<std::string>{"cl", "clang-cl"}
      : backend == "xcode" ? std::vector<std::string>{"/usr/bin/clang++"}
                           : std::vector<std::string>{"clang++", "g++", "cl"};
  const std::vector<std::string> c_defaults =
      backend == "visual-studio" || backend == "nmake" || backend == "jom"
          ? std::vector<std::string>{"cl", "clang-cl"}
      : backend == "xcode" ? std::vector<std::string>{"/usr/bin/clang"}
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
    auto name = fs::u8path(compiler).stem().u8string();
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

  tools.microsoft_abi = tools.msvc;
#ifdef _WIN32
  // GNU-style Clang supports both MinGW and MSVC targets. Query the driver
  // rather than assuming its ABI from its filename or the host operating
  // system.
  if (!tools.msvc) {
    const auto uses_microsoft_abi = [&](const std::string &compiler) {
      if (const auto output = capture({compiler, "-dumpmachine"}, project.root);
          output.status == 0)
        return output.text.find("-windows-msvc") != std::string::npos;

      throw std::runtime_error("cannot determine compiler target: " + compiler);
    };
    tools.microsoft_abi = uses_microsoft_abi(cpp ? tools.cxx : tools.cc);
    if (c && cpp && uses_microsoft_abi(tools.cc) != tools.microsoft_abi)
      throw std::runtime_error("C and C++ compilers must target the same ABI");
  }
#endif

  if (cpp && tools.msvc && project.cxx_standard == "11")
    throw std::runtime_error("MSVC-style drivers require C++ std 14 or newer");

  return tools;
}

void resolve_pch_drivers(Toolchain &tools, const Project &project) {
  // External interfaces can add prefix headers after compiler selection.
  // Inspect the completed project so those requests receive the same probes.
  bool pch_c = !project.pch_c.empty(), pch_cpp = !project.pch_cpp.empty();
  for (const auto *const group : {&project.targets, &project.tests})
    for (const auto &target : *group) {
      pch_c = pch_c || !target.pch_c.empty();
      pch_cpp = pch_cpp || !target.pch_cpp.empty();
    }

  // Probe only when needed; executable aliases do not reliably identify
  // GCC/Clang.
  const auto pch_driver = [&](const std::string &compiler) {
    if (tools.msvc)
      return std::string("msvc");

    const auto output = capture({compiler, "--version"}, project.root);
    if (output.status == 0 && output.text.find("clang") != std::string::npos)
      return std::string("clang");

    if (output.status == 0 &&
        (output.text.find("Free Software Foundation") != std::string::npos ||
         output.text.find("GCC") != std::string::npos))
      return std::string("gcc");

    throw std::runtime_error(
        "PCH requires a recognized GCC, Clang or MSVC compiler");
  };
  if (!tools.cc.empty() && pch_c)
    tools.c_pch = pch_driver(tools.cc);

  if (!tools.cxx.empty() && pch_cpp)
    tools.cpp_pch = pch_driver(tools.cxx);
}

/// Retain resolved executables for the next configuration of this profile.
void save_toolchain(const Toolchain &tools, const Project &project) {
  const toml::table cache{{"cc", tools.cc}, {"cxx", tools.cxx}};
  std::ostringstream text;
  text << cache << '\n';
  write_if_changed(project.build / layout::toolchain_cache, text.str());
}
}  // namespace clutch
