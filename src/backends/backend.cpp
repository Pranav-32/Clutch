/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Discover native generator tools and dispatch generation/build
 * commands.
 */

#include "backend.hpp"

#include "backends/make/make.hpp"
#include "backends/ninja/ninja.hpp"
#include "backends/visual_studio/visual_studio.hpp"
#include "backends/xcode/xcode.hpp"
#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/environment.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include <toml.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace clutch {
namespace {
enum class Host {
  Any,
  Windows,
  MacOS
};

/// Check generator host restrictions without probing installed executables.
bool supports(const Host host) {
  if (host == Host::Any)
    return true;

#ifdef _WIN32
  return host == Host::Windows;
#elif defined(__APPLE__)
  return host == Host::MacOS;
#else
  return false;
#endif
}

/// Select exact output targets and translate job limits to Ninja's flags.
std::vector<std::string>
ninja_build_command(const Graph &graph,
                    const Options &options,
                    const std::vector<size_t> &target_indices) {
  std::vector<std::string> command{graph.backend.executable,
                                   "-C",
                                   graph.project.build.u8string()};
  if (options.jobs > 0)
    command.insert(command.end(), {"-j", std::to_string(options.jobs)});

  for (const auto index : target_indices)
    command.push_back(graph.targets.at(index).output.generic_u8string());

  return command;
}

/// Couple each native format to its host policy, discovery and execution hooks.
struct BackendDriver {
  std::string name, description;
  Host host;
  const char *environment_variable;
  std::vector<const char *> executable_names;
  void (*generate)(const Graph &, const Options &, const fs::path &);
  std::vector<std::string> (*build)(const Graph &,
                                    const Options &,
                                    const std::vector<size_t> &);
  std::string (*discover)() = nullptr;
};

/// Return the process-lifetime registry of implemented generator drivers.
const std::vector<BackendDriver> &drivers() {
  // A driver owns both serialization and invocation: the CLI never assumes a
  // particular backend's command-line flags.
  static const std::vector<BackendDriver> registered{
      {"ninja",
       "Ninja",
       Host::Any,
       "NINJA",
       {"ninja", "ninja-build"},
       generate_ninja,
       ninja_build_command,
       discover_ninja},
      {"make",
       "GNU Make (Unix, MinGW and MSYS)",
       Host::Any,
       "MAKE",
       {"make", "gmake", "mingw32-make"},
       generate_make,
       make_build_command},
      {"nmake",
       "Microsoft NMake",
       Host::Windows,
       "NMAKE",
       {"nmake"},
       generate_make,
       make_build_command},
      {"jom",
       "JOM (parallel NMake)",
       Host::Windows,
       "JOM",
       {"jom"},
       generate_make,
       make_build_command},
      {"visual-studio",
       "Visual Studio / MSBuild",
       Host::Windows,
       "MSBUILD",
       {"MSBuild", "msbuild"},
       generate_visual_studio,
       visual_studio_build_command},
      {"xcode",
       "Xcode",
       Host::MacOS,
       "XCODEBUILD",
       {"xcodebuild"},
       generate_xcode,
       xcode_build_command}};
  return registered;
}

/// Resolve a normalized generator or list implemented choices in the
/// diagnostic.
const BackendDriver &driver_for(const std::string &name) {
  const auto normalized = normalize_backend_name(name);
  std::string available;
  for (const auto &driver : drivers()) {
    if (driver.name == normalized)
      return driver;

    if (!available.empty())
      available += ", ";

    available += driver.name;
  }

  throw std::runtime_error("unsupported build generator: " + name +
                           "; implemented generators: " + available);
}
}  // namespace

/// Map accepted display-name aliases onto stable Clutch generator identifiers.
std::string normalize_backend_name(const std::string &name) {
  auto normalized = name;
  std::transform(normalized.begin(),
                 normalized.end(),
                 normalized.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });

  if (normalized == "unix makefiles" || normalized == "unix-makefiles" ||
      normalized == "mingw makefiles" || normalized == "mingw-makefiles" ||
      normalized == "msys makefiles" || normalized == "msys-makefiles" ||
      normalized == "gnu make" || normalized == "gmake")
    return "make";

  if (normalized == "nmake makefiles" || normalized == "nmake-makefiles")
    return "nmake";

  if (normalized == "nmake makefiles jom" ||
      normalized == "nmake-makefiles-jom")
    return "jom";

  if (normalized == "visual studio" || normalized == "vs" ||
      normalized == "msbuild")
    return "visual-studio";

  return normalized;
}

/// Expose registry metadata for the CLI without requiring a project or tool
/// search.
std::vector<GeneratorInfo> supported_generators() {
  std::vector<GeneratorInfo> result;
  for (const auto &driver : drivers())
    result.push_back({driver.name, driver.description, supports(driver.host)});

  return result;
}

/// Resolve the executor while retaining the user's selected native build
/// format.
BuildBackend resolve_backend(const Options &options, const Project &project) {
  const auto &driver = driver_for(project.generator);
  if (!supports(driver.host))
    throw std::runtime_error("generator " + driver.name +
                             " is not available on this operating system");

  // Explicit selection wins over generic/backend environment variables and
  // cache.
  auto requested = options.build_tool_executable;

  if (const auto value = environment_value("CLUTCH_BUILD_TOOL");
      requested.empty() && value)
    requested = *value;

  if (const auto value = environment_value(driver.environment_variable);
      requested.empty() && value)
    requested = *value;

  if (const auto cache_path = project.build / layout::backend_cache;
      requested.empty() && fs::exists(cache_path)) {
    if (const auto cache = toml::parse_file(cache_path.u8string());
        cache["name"].value_or(std::string{}) == driver.name)
      requested = cache["executable"].value_or(std::string{});
  }

  if (!requested.empty()) {
    if (const auto executable = find_program(requested); !executable.empty())
      return {driver.name, executable};

    throw std::runtime_error("cannot find build generator executable: " +
                             requested + "; use --build-tool when configuring");
  }

  // Search ordinary executable names before optional IDE-bundled installations.
  for (const auto *const candidate : driver.executable_names) {
    if (const auto executable = find_program(candidate); !executable.empty())
      return {driver.name, executable};
  }

  if (driver.discover) {
    if (const auto executable = driver.discover(); !executable.empty())
      return {driver.name, executable};
  }

  throw std::runtime_error(
      "selected build generator was not found: " + driver.name +
      "; install it or set --build-tool when configuring");
}

/// Cache the selected format/executor for subsequent explicit configurations.
void save_backend(const BuildBackend &backend, const Project &project) {
  const toml::table cache{{"name", backend.name},
                          {"executable", backend.executable}};
  std::ostringstream text;
  text << cache << '\n';
  write_if_changed(project.build / layout::backend_cache, text.str());
}

/// Dispatch serialization; driver generation must not compile the main project.
void generate_backend_files(const Graph &graph,
                            const Options &options,
                            const fs::path &executable) {
  driver_for(graph.backend.name).generate(graph, options, executable);
}

/// Check the saved driver and target selection before constructing its
/// invocation.
std::vector<std::string>
backend_build_command(const Graph &graph,
                      const Options &options,
                      const std::vector<size_t> &target_indices) {
  const auto &driver = driver_for(graph.backend.name);
  if (!supports(driver.host))
    throw std::runtime_error(
        "configured generator is unavailable on this host");

  if (target_indices.empty())
    throw std::runtime_error("no build targets were selected");

  for (const auto index : target_indices) {
    if (index >= graph.targets.size())
      throw std::runtime_error("invalid build target selection");
  }

  auto execution = options;
  if (execution.jobs == 0) {
    const auto processors = std::max(1U, std::thread::hardware_concurrency());
    execution.jobs = static_cast<int>(
        std::min(processors,
                 static_cast<unsigned>(std::numeric_limits<int>::max())));
  }

  return driver.build(graph, execution, target_indices);
}
}  // namespace clutch
