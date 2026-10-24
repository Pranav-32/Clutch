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
#include "platform/capture.hpp"
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
#include <utility>
#include <vector>

namespace clutch {
namespace {
/// Ninja's clean tool follows the selected producer closure and preserves
/// rules.
int ninja_clean_artifacts(const Graph &graph,
                          const std::vector<size_t> &target_indices) {
  std::vector<std::string> command{graph.backend.executable,
                                   "-C",
                                   graph.project.build.u8string(),
                                   "-t",
                                   "clean"};
  for (const auto index : target_indices)
    command.push_back(graph.targets.at(index).output.generic_u8string());

  return run(command, graph.project.build);
}

/// Adapt controlled filesystem cleanup to the registry's action contract.
int xcode_clean_artifacts(const Graph &graph,
                          const std::vector<size_t> &target_indices) {
  clean_xcode_targets(graph, target_indices);
  return 0;
}

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
  int (*clean)(const Graph &, const std::vector<size_t> &) = nullptr;
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
       discover_ninja,
       ninja_clean_artifacts},
      {"make",
       "GNU Make (Unix, MinGW and MSYS)",
       Host::Any,
       "MAKE",
       {"gmake", "make", "mingw32-make"},
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
       xcode_build_command,
       nullptr,
       xcode_clean_artifacts}};
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

/// Verify the executor understands the features emitted by its generator.
void validate_executor(const std::string &name,
                       const std::string &executable,
                       const fs::path &root) {
  if (name == "make") {
    validate_gnu_make(executable, root);
  } else if (name == "ninja") {
    constexpr auto minimum_major = 1U, minimum_minor = 10U;
    const auto result = capture({executable, "--version"}, root);
    std::istringstream version(result.text);
    unsigned major = 0, minor = 0;
    char separator = '\0';
    version >> major >> separator >> minor;
    if (result.status || !version || separator != '.' ||
        major < minimum_major ||
        (major == minimum_major && minor < minimum_minor))
      throw std::runtime_error(
          "the Ninja generator requires Ninja 1.10 or newer");
  } else if (name == "xcode") {
    if (const auto result = capture({executable, "-version"}, root);
        result.status)
      throw std::runtime_error(
          "xcodebuild requires an active full Xcode installation");
  }
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

/// Dispatch optional preparation through the same registry as native builds.
int clean_backend_artifacts(const Graph &graph,
                            const std::vector<size_t> &target_indices) {
  const auto &driver = driver_for(graph.backend.name);
  if (!supports(driver.host))
    throw std::runtime_error(
        "configured generator is unavailable on this host");

  if (target_indices.empty())
    throw std::runtime_error("no build targets were selected for cleaning");

  for (const auto index : target_indices)
    if (index >= graph.targets.size())
      throw std::runtime_error("invalid build target selection");

  return driver.clean ? driver.clean(graph, target_indices) : 0;
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
      if (const auto executable =
              find_program(cache["executable"].value_or(std::string{}));
          !executable.empty())
        requested = executable;
  }

  if (!requested.empty()) {
    if (const auto executable = find_program(requested); !executable.empty()) {
      validate_executor(driver.name, executable, project.root);
      return {driver.name, executable};
    }

    throw std::runtime_error("cannot find build generator executable: " +
                             requested + "; use --build-tool when configuring");
  }

  // Search ordinary executable names before optional IDE-bundled installations.
  std::string failures;
  for (const auto *const candidate : driver.executable_names) {
    if (const auto executable = find_program(candidate); !executable.empty()) {
      try {
        validate_executor(driver.name, executable, project.root);
        return {driver.name, executable};
      } catch (const std::runtime_error &error) {
        failures += "\n  " + executable + ": " + error.what();
      }
    }
  }

  if (driver.discover) {
    if (const auto executable = driver.discover(); !executable.empty()) {
      validate_executor(driver.name, executable, project.root);
      return {driver.name, executable};
    }
  }

  throw std::runtime_error(
      "selected build generator was not found: " + driver.name +
      "; install it or set --build-tool when configuring" + failures);
}

BuildTools resolve_build_tools(const Options &options, const Project &project) {
  // Resolve compilers against a concrete format before accepting it. In
  // particular, MSBuild cannot honor GNU drivers and Xcode cannot honor GCC.
  const auto resolve = [&](const std::string &name) {
    auto selected = project;
    selected.generator = name;
    auto backend = resolve_backend(options, selected);
    auto toolchain = resolve_toolchain(options, selected);
    if (backend.name == "visual-studio" && !toolchain.msvc)
      throw std::runtime_error(
          "the Visual Studio backend requires an MSVC compiler");

    if (backend.name == "xcode")
      validate_xcode_toolchain(toolchain);

    return BuildTools{std::move(backend), std::move(toolchain)};
  };
  if (!project.generator.empty())
    return resolve(project.generator);

  // An explicit executor is never tried against arbitrary output formats.
  // Standard executable names identify their family; wrappers need a generator.
  auto requested = options.build_tool_executable;
  if (const auto value = environment_value("CLUTCH_BUILD_TOOL");
      requested.empty() && value)
    requested = *value;

  if (!requested.empty()) {
    auto name = fs::u8path(requested).filename().u8string();
    std::transform(name.begin(),
                   name.end(),
                   name.begin(),
                   [](const unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    if (fs::u8path(name).extension() == ".exe")
      name = fs::u8path(name).stem().u8string();

    for (const auto &driver : drivers())
      if (std::find(driver.executable_names.begin(),
                    driver.executable_names.end(),
                    name) != driver.executable_names.end())
        return resolve(driver.name);

    throw std::runtime_error(
        "cannot infer generator from build tool: " + requested +
        "; specify --generator for a custom executor");
  }

  std::vector<std::string> candidates;
  if (const auto cache_path = project.build / layout::backend_cache;
      fs::exists(cache_path)) {
    const auto cache = toml::parse_file(cache_path.u8string());
    candidates.push_back(cache["name"].value_or(std::string{}));
  }
  for (const auto &driver : drivers())
    if (supports(driver.host) &&
        std::find(candidates.begin(), candidates.end(), driver.name) ==
            candidates.end())
      candidates.push_back(driver.name);

  std::string failures;
  for (const auto &name : candidates) {
    if (name.empty())
      continue;

    try {
      return resolve(name);
    } catch (const std::runtime_error &error) {
      failures += "\n  " + name + ": " + error.what();
    }
  }
  throw std::runtime_error(
      "no available compatible build generator; install a supported "
      "executor or select --generator and --build-tool" +
      failures);
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
