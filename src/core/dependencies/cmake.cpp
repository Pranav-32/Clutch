/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Configure isolated CMake libraries and consume their evaluated file
 * API.
 */
#include "cmake.hpp"
#include "backends/backend.hpp"
#include "backends/ninja/ninja.hpp"
#include "core/options.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include "toolchains/toolchain.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clutch::dependencies {
namespace {
using Json = nlohmann::json;
// File API objects are bounded metadata, not arbitrary unbounded input streams.
constexpr auto maximum_metadata_bytes = 16U * 1024U * 1024U;
constexpr auto maximum_cmake_targets = 4096U;

/// CMake bracket arguments do not expand variables, quotes or backslashes.
std::string literal(const std::string &value) {
  std::string delimiter = "=";
  while (value.find("]" + delimiter + "]") != value.npos)
    delimiter += '=';
  return "[" + delimiter + "[" + value + "]" + delimiter + "]";
}

/// Reject command/variable names before embedding them into generated CMake.
bool identifier(const std::string &value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char c) {
           return std::isalnum(c) || c == '_' || c == '-';
         });
}

/// Follow only metadata files directly inside CMake's reply directory.
Json read_json(const fs::path &file) {
  validate_generated_path(file);
  if (!fs::is_regular_file(file) ||
      fs::file_size(file) > maximum_metadata_bytes)
    throw std::runtime_error("invalid or oversized CMake metadata: " +
                             file.u8string());
  std::ifstream stream(file, std::ios::binary);
  auto value = Json::parse(stream);
  if (stream.bad())
    throw std::runtime_error("cannot read CMake metadata: " + file.u8string());
  return value;
}

/// Decode native command fragments without executing a shell. CMake uses
/// shell quoting on POSIX and CRT quoting for Microsoft driver command lines.
std::vector<std::string> split(const std::string &text, const bool microsoft) {
  std::vector<std::string> result;
  std::string token;
  char quote = 0;
  bool present = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (microsoft && c == '\\') {
      size_t count = 1;
      while (i + count < text.size() && text[i + count] == '\\')
        ++count;
      if (i + count < text.size() && text[i + count] == '"') {
        token.append(count / 2, '\\');
        if (count % 2 != 0)
          token += '"';
        else
          quote = quote ? 0 : '"';
        i += count;
      } else {
        token.append(count, '\\');
        i += count - 1;
      }
      present = true;
    } else if (!microsoft && c == '\\' && quote != '\'') {
      if (++i == text.size())
        throw std::runtime_error("incomplete escape in CMake command fragment");
      token += text[i];
      present = true;
    } else if (c == '"' || (!microsoft && c == '\'')) {
      if (!quote)
        quote = c;
      else if (quote == c)
        quote = 0;
      else
        token += c;
      present = true;
    } else if (!quote && std::isspace(static_cast<unsigned char>(c))) {
      if (present) {
        result.push_back(token);
        token.clear();
        present = false;
      }
    } else {
      token += c;
      present = true;
    }
  }
  if (quote)
    throw std::runtime_error("unclosed quote in CMake command fragment");

  if (present)
    result.push_back(token);
  return result;
}

/// Resolve reply names as filenames, rejecting redirects outside the reply.
fs::path reply_file(const fs::path &reply, const Json &reference) {
  const auto name = reference.at("jsonFile").get<std::string>();
  if (fs::u8path(name).filename() != fs::u8path(name))
    throw std::runtime_error("invalid CMake reply filename");
  return reply / fs::u8path(name);
}

/// Extract a consumer's compile interface for exactly one source language.
void compile_usage(DependencyUsage &usage,
                   const Json &target,
                   const bool c,
                   const bool microsoft,
                   std::vector<fs::path> &interface_dirs) {
  // Module sources can carry private per-file definitions. Select the probe's
  // own source group instead of merging unrelated source/language settings.
  const auto *const filename = c ? "c.c" : "cpp.cpp";
  const auto &sources = target.at("sources");
  const auto probe =
      std::find_if(sources.begin(), sources.end(), [&](const Json &source) {
        const auto path = fs::u8path(source.at("path").get<std::string>());
        return path == fs::path(filename) ||
               path == fs::u8path(
                           target.at("paths").at("source").get<std::string>()) /
                           filename;
      });
  if (probe == sources.end() || !probe->contains("compileGroupIndex"))
    throw std::runtime_error("CMake consumer probe has no compile group");

  const auto &group = target.at("compileGroups")
                          .at(probe->at("compileGroupIndex").get<size_t>());
  if (group.at("language") != (c ? "C" : "CXX"))
    throw std::runtime_error("CMake consumer probe has the wrong language");

  auto &flags = c ? usage.c_options : usage.cpp_options;
  for (const auto &include : group.value("includes", Json::array())) {
    const auto path = include.at("path").get<std::string>();
    // Retain physical roots for preparation stamps without mixing C-only and
    // C++-only include flags in the public compile interface.
    const auto directory = fs::u8path(path);
    if (std::find(interface_dirs.begin(), interface_dirs.end(), directory) ==
        interface_dirs.end())
      interface_dirs.push_back(directory);
    if (include.value("isSystem", false)) {
      if (microsoft) {
        if (std::find(flags.begin(), flags.end(), "/external:W0") ==
            flags.end())
          flags.emplace_back("/external:W0");
        flags.push_back("/external:I" + path);
      } else
        flags.insert(flags.end(), {"-isystem", path});
    } else
      flags.push_back((microsoft ? "/I" : "-I") + path);
  }
  for (const auto &define : group.value("defines", Json::array()))
    flags.push_back((microsoft ? "/D" : "-D") +
                    define.at("define").get<std::string>());
  auto &standard = c ? usage.c_standard : usage.cpp_standard;
  if (group.contains("languageStandard"))
    standard = group.at("languageStandard").at("standard").get<std::string>();
  for (const auto &fragment :
       group.value("compileCommandFragments", Json::array())) {
    if (!fragment.contains("backtrace"))
      continue;
    for (const auto &flag :
         split(fragment.at("fragment").get<std::string>(), microsoft))
      // The consumer's profile owns CRT/optimization/debug policy. Language
      // requirements are represented separately and checked before
      // generation.
      if (flag.rfind("-std=", 0) != 0 && flag.rfind("/std:", 0) != 0)
        flags.push_back(flag);
  }
}

/// Only CMake's link interface enters the consumer command; no source/object
/// paths from the upstream project are flattened into Clutch translation units.
void link_usage(DependencyUsage &usage,
                const Json &target,
                const fs::path &build,
                const std::set<fs::path> &artifacts,
                const bool microsoft) {
  if (!target.contains("link"))
    return;
  usage.needs_cpp = target.at("link").value("language", std::string{}) == "CXX";
  for (const auto &fragment :
       target.at("link").value("commandFragments", Json::array())) {
    const auto role = fragment.at("role").get<std::string>();
    if (role == "flags" && !fragment.contains("backtrace"))
      continue;
    for (auto flag :
         split(fragment.at("fragment").get<std::string>(), microsoft)) {
      if (role == "libraries" && !flag.empty() && flag.front() != '-' &&
          (flag.front() != '/' || fs::u8path(flag).is_absolute())) {
        const auto path = fs::u8path(flag);
        const auto absolute = fs::absolute(build / path).lexically_normal();
        if (path.is_absolute() || path.has_parent_path() ||
            artifacts.count(absolute)) {
          usage.link_inputs.push_back(absolute);
          flag = absolute.u8string();
        }
      }
      usage.link_options.push_back(std::move(flag));
    }
  }
}
}  // namespace

NativeInterface inspect_cmake(const ForeignRequest &request,
                              const Project &project,
                              const Toolchain &tools,
                              const BuildBackend &backend,
                              const Options &options) {
  const auto &r = request.requirement;
  const auto cmake = find_program("cmake");
  if (cmake.empty())
    throw std::runtime_error("dependency '" + r.alias +
                             "' requires CMake 3.21 or newer");
  auto targets = r.targets;
  if (!r.target.empty())
    targets.push_back(r.target);

  std::string selection =
      request.identity + ':' + r.version + ':' + r.cmake_package;
  for (const auto &component : r.components)
    selection += ':' + component;
  for (const auto &target : targets)
    selection += ':' + target;
  for (const auto &define : r.defines)
    selection += ':' + std::to_string(define.size()) + ':' + define;
  if (options.discover_targets)
    selection += ":discovery";
  const auto id = source_key(selection);
  const auto directory = project.build / layout::environment / "foreign" / id;
  const auto source = directory / "probe";
  const auto build = directory / "build";
  validate_generated_path(directory);
  std::ostringstream script;
  script << "cmake_minimum_required(VERSION 3.21)\n"
            "cmake_policy(SET CMP0091 NEW)\n"
            "set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)\n"
            "set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)\n"
            "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
         << "set(CMAKE_C_STANDARD " << literal(project.c_standard) << ")\n"
         << "set(CMAKE_CXX_STANDARD " << literal(project.cxx_standard) << ")\n"
         << "set(CMAKE_C_STANDARD_REQUIRED ON)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  if (tools.microsoft_abi || tools.msvc)
    script << "set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded"
           << (options.release ? "" : "Debug")
           << (project.runtime == "dynamic" ? "DLL" : "") << ")\n";

  script << "project(ClutchDependencyProbe LANGUAGES C CXX)\n"
            "file(REMOVE \"${CMAKE_BINARY_DIR}/package-version.txt\")\n"
            "set(BUILD_TESTING OFF CACHE BOOL \"\")\n";
  if (options.offline)
    script << "set(FETCHCONTENT_FULLY_DISCONNECTED ON)\n"
              "set(FETCHCONTENT_UPDATES_DISCONNECTED ON)\n";

  if (!r.root.empty() && r.provider == "system")
    script << "list(PREPEND CMAKE_PREFIX_PATH " << literal(r.root) << ")\n";
  if (r.provider == "system") {
    const auto package = !r.cmake_package.empty() ? r.cmake_package : r.system;
    if (!identifier(package))
      throw std::runtime_error("invalid CMake package name: " + package);
    script << "find_package(" << package;
    if (!r.version.empty()) {
      if (!std::all_of(r.version.begin(),
                       r.version.end(),
                       [](const unsigned char ch) {
                         return std::isalnum(ch) || ch == '.' || ch == '<';
                       }))
        throw std::runtime_error("system dependency.version must use a CMake "
                                 "version or version range");
      script << ' ' << r.version;
    }
    script << " REQUIRED";
    if (!r.components.empty()) {
      script << " COMPONENTS";
      for (const auto &component : r.components)
        script << ' ' << literal(component);
    }
    script << ")\n";
    script << "if(DEFINED " << package
           << "_VERSION)\nfile(WRITE "
              "\"${CMAKE_BINARY_DIR}/package-version.txt\" \"${"
           << package << "_VERSION}\")\nendif()\n";
  } else {
    if (!fs::is_regular_file(request.source / "CMakeLists.txt"))
      throw std::runtime_error("dependency has no CMakeLists.txt: " + r.alias);
    script << "add_subdirectory(" << literal(request.source.generic_u8string())
           << " upstream EXCLUDE_FROM_ALL)\n";
  }
  // Directory properties enumerate real targets, including INTERFACE and
  // imported libraries that have no standalone codemodel build target. CMake
  // does not enumerate aliases; their underlying target names remain usable.
  script << R"cmake(
function(clutch_collect_targets directory)
  get_property(_local DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_imported DIRECTORY "${directory}" PROPERTY IMPORTED_TARGETS)
  foreach(_target IN LISTS _local _imported)
    if(NOT TARGET "${_target}")
      continue()
    endif()
    get_target_property(_type "${_target}" TYPE)
    if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|INTERFACE_LIBRARY|UNKNOWN_LIBRARY)$")
      set_property(GLOBAL APPEND PROPERTY clutch_libraries "${_target}")
      file(APPEND "${CMAKE_BINARY_DIR}/available-targets.txt" "${_target}\t${_type}\n")
    endif()
  endforeach()
  get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_child IN LISTS _children)
    clutch_collect_targets("${_child}")
  endforeach()
endfunction()
file(WRITE "${CMAKE_BINARY_DIR}/available-targets.txt" "")
clutch_collect_targets("${CMAKE_CURRENT_SOURCE_DIR}")
)cmake";
  script << "set(_clutch_selected";
  for (const auto &target : targets)
    script << ' ' << literal(target);
  script << ")\n";
  if (options.discover_targets)
    script << "return()\n";
  else
    script << R"cmake(
if(NOT _clutch_selected)
  get_property(_clutch_selected GLOBAL PROPERTY clutch_libraries)
  list(REMOVE_DUPLICATES _clutch_selected)
  list(LENGTH _clutch_selected _count)
  if(NOT _count EQUAL 1)
    message(FATAL_ERROR "Dependency has ${_count} library targets: ${_clutch_selected}. Select target/targets in clutch.toml; use clutch . --targets to list libraries.")
  endif()
endif()
)cmake";
  // Keep selected names as CMake variable expansions in the generated loop.
  // Explicit names were supplied as bracket literals, never executable code.
  script << R"cmake(
add_custom_target(clutch_dependency)
foreach(_clutch_target IN LISTS _clutch_selected)
  if(NOT TARGET "${_clutch_target}")
    message(FATAL_ERROR "dependency does not export target: ${_clutch_target}")
  endif()
  get_target_property(_kind "${_clutch_target}" TYPE)
  if(NOT _kind MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|INTERFACE_LIBRARY|UNKNOWN_LIBRARY)$")
    message(FATAL_ERROR "dependency target must be a library: ${_clutch_target}")
  endif()
  add_dependencies(clutch_dependency "${_clutch_target}")
endforeach()
)cmake";
  // INTERFACE libraries may publish implementation sources (JUCE modules, for
  // example). A native archive compiles them with CMake's evaluated settings,
  // dependencies and generated-file rules. Probe executables are never built.
  script << "add_library(clutch_interface_sources STATIC EXCLUDE_FROM_ALL "
            "bridge.c)\n"
            "set_target_properties(clutch_interface_sources PROPERTIES "
            "POSITION_INDEPENDENT_CODE ON)\n"
            "target_link_libraries(clutch_interface_sources PRIVATE "
            "${_clutch_selected})\n"
            "add_dependencies(clutch_dependency clutch_interface_sources)\n";
  if (!r.defines.empty()) {
    script << "target_compile_definitions(clutch_interface_sources PRIVATE";
    for (const auto &define : r.defines)
      script << ' ' << literal(define);
    script << ")\n";
  }
  for (const auto *const language : {"c", "cpp"}) {
    const auto probe = std::string("clutch_probe_") + language;
    script << "add_executable(" << probe << " EXCLUDE_FROM_ALL " << language
           << "." << language << ")\n"
           << "set_target_properties(" << probe
           << " PROPERTIES EXPORT_COMPILE_COMMANDS OFF)\n"
           << "target_link_libraries(" << probe
           << " PRIVATE clutch_interface_sources ${_clutch_selected})\n";
  }
  // Generate runtime paths with evaluated target expressions. Imported DLLs
  // retain their runtime/import-library distinction; interfaces emit no file.
  script << "file(GENERATE OUTPUT "
            "\"${CMAKE_BINARY_DIR}/runtime-$<CONFIG>.txt\" CONTENT "
            "\"$<JOIN:$<TARGET_RUNTIME_DLLS:clutch_probe_cpp>,\\n>\")\n";
  write_if_changed(source / "CMakeLists.txt", script.str());
  write_if_changed(source / "c.c", "int main(void) { return 0; }\n");
  write_if_changed(source / "cpp.cpp", "int main() { return 0; }\n");
  write_if_changed(source / "bridge.c",
                   "typedef int clutch_empty_translation_unit;\n");
  const auto query = build / ".cmake/api/v1/query";
  write_if_changed(query / "codemodel-v2", "");
  write_if_changed(query / "cmakeFiles-v1", "");

  std::vector<std::string> command{
      cmake,
      "-S",
      source.u8string(),
      "-B",
      build.u8string(),
      "-DCMAKE_BUILD_TYPE=" +
          std::string(options.release ? "Release" : "Debug"),
      "-DCMAKE_C_COMPILER=" + tools.cc,
      "-DCMAKE_CXX_COMPILER=" + tools.cxx};
  if (r.provider == "system" && !r.root.empty())
    command.push_back("-D" +
                      (r.cmake_package.empty() ? r.system : r.cmake_package) +
                      "_ROOT=" + r.root);
  auto ninja =
      backend.name == "ninja" ? backend.executable : find_program("ninja");
  if (ninja.empty())
    ninja = discover_ninja();
  if (!ninja.empty())
    command.insert(command.end(),
                   {"-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja});
  for (const auto &[key, value] : r.cmake_options) {
    if (!identifier(key) || key.rfind("CMAKE_", 0) == 0 ||
        key.rfind("VCPKG_", 0) == 0 || key == "BUILD_TESTING")
      throw std::runtime_error(
          "reserved or invalid dependency.cmake_options key: " + key);
    auto argument = "-D" + key;
    argument += '=';
    argument += value;
    command.push_back(std::move(argument));
  }
  if (const int code = run(command, project.root))
    throw std::runtime_error("CMake configuration failed for " + r.alias +
                             " (exit " + std::to_string(code) + ")");

  NativeInterface result;
  const auto catalog = build / "available-targets.txt";
  validate_generated_path(catalog);
  if (!fs::is_regular_file(catalog) ||
      fs::file_size(catalog) > maximum_metadata_bytes)
    throw std::runtime_error("invalid CMake target catalog");
  std::ifstream catalog_stream(catalog);
  for (std::string line; std::getline(catalog_stream, line);) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (const auto separator = line.find('\t');
        separator != line.npos &&
        result.available_targets.size() < maximum_cmake_targets)
      result.available_targets.emplace_back(line.substr(0, separator),
                                            line.substr(separator + 1));
    else
      throw std::runtime_error("invalid or oversized CMake target catalog");
  }
  if (catalog_stream.bad())
    throw std::runtime_error("cannot read CMake target catalog");

  std::sort(result.available_targets.begin(), result.available_targets.end());
  if (options.discover_targets)
    return result;

  const auto reply = build / ".cmake/api/v1/reply";
  fs::path index;
  for (const auto &entry : fs::directory_iterator(reply))
    if (const auto name = entry.path().filename().u8string();
        name.rfind("index-", 0) == 0 && entry.path() > index)
      index = entry.path();
  if (index.empty())
    throw std::runtime_error("CMake did not publish a file API index");
  const auto response = read_json(index);
  const auto codemodel =
      read_json(reply_file(reply, response.at("reply").at("codemodel-v2")));
  const auto &configurations = codemodel.at("configurations");
  const auto configuration = std::find_if(
      configurations.begin(),
      configurations.end(),
      [&](const Json &value) {
        return value.at("name") == (options.release ? "Release" : "Debug");
      });
  if (configuration == configurations.end())
    throw std::runtime_error(
        "CMake did not produce the selected build configuration");
  const auto &references = configuration->at("targets");
  if (references.size() > maximum_cmake_targets)
    throw std::runtime_error(
        "CMake dependency exceeds the target metadata limit");

  result.usage.id = id;
  result.build = {id,
                  build,
                  {cmake,
                   "--build",
                   build.u8string(),
                   "--config",
                   options.release ? "Release" : "Debug",
                   "--target",
                   "clutch_dependency",
                   "--parallel",
                   "@CLUTCH_JOBS@"},
                  {}};
  result.build.compilation_database = build / "compile_commands.json";
  result.build.rebuild_arguments = result.build.arguments;
  result.build.rebuild_arguments.emplace_back("--clean-first");
  std::set<fs::path> artifact_paths;
  // Inventory producers before decoding link fragments: a root-level native
  // artifact has a bare filename, unlike a system library with the same suffix.
  for (const auto &reference : references) {
    if (reference.at("name") == "clutch_probe_c" ||
        reference.at("name") == "clutch_probe_cpp")
      continue;

    // Discard each model after extracting its artifacts; large native graphs
    // must not retain every target's sources and compile groups in memory.
    const auto target = read_json(reply_file(reply, reference));
    for (const auto &artifact : target.value("artifacts", Json::array()))
      artifact_paths.insert(
          fs::absolute(build /
                       fs::u8path(artifact.at("path").get<std::string>()))
              .lexically_normal());
  }
  for (const auto &reference : references) {
    const auto name = reference.at("name").get<std::string>();
    if (name == "clutch_probe_c" || name == "clutch_probe_cpp") {
      const auto target = read_json(reply_file(reply, reference));
      const bool c = name == "clutch_probe_c";
      compile_usage(result.usage,
                    target,
                    c,
                    tools.msvc,
                    result.build.interface_dirs);
      if (c && target.contains("link"))
        result.usage.needs_cpp =
            target.at("link").value("language", std::string{}) == "CXX";
      if (!c) {
        const bool needs_cpp = result.usage.needs_cpp;
        link_usage(result.usage, target, build, artifact_paths, tools.msvc);
        result.usage.needs_cpp = needs_cpp;
      }
    }
  }
  for (const auto &input : result.usage.link_inputs)
    if (artifact_paths.count(input))
      result.build.artifacts.push_back(input);

  if (const auto runtime =
          build / (std::string("runtime-") +
                   (options.release ? "Release" : "Debug") + ".txt");
      fs::is_regular_file(runtime)) {
    std::ifstream stream(runtime);
    for (std::string line; std::getline(stream, line);)
      if (!line.empty())
        result.usage.runtime_libraries.push_back(fs::u8path(line));
  }
  if (const auto version = build / "package-version.txt";
      fs::is_regular_file(version)) {
    std::ifstream stream(version);
    std::getline(stream, result.version);
  }
  const auto files =
      read_json(reply_file(reply, response.at("reply").at("cmakeFiles-v1")));
  for (const auto &input : files.at("inputs")) {
    const auto path =
        fs::absolute(source / fs::u8path(input.at("path").get<std::string>()))
            .lexically_normal();
    if (!input.value("isGenerated", false) && !input.value("isCMake", false) &&
        path.string().find(directory.string()) != 0 &&
        fs::is_regular_file(path))
      result.inputs.push_back({path, input_signature(path)});
  }
  return result;
}
}  // namespace clutch::dependencies
