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
                   const bool microsoft) {
  for (const auto &group : target.value("compileGroups", Json::array())) {
    auto &flags = c ? usage.c_options : usage.cpp_options;
    for (const auto &include : group.value("includes", Json::array())) {
      const auto path = include.at("path").get<std::string>();
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
}

/// Only CMake's link interface enters the consumer command; no source/object
/// paths from the upstream project are flattened into Clutch translation units.
void link_usage(DependencyUsage &usage,
                const Json &target,
                const fs::path &build,
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
        if (path.is_absolute() || path.has_parent_path()) {
          const auto absolute = fs::absolute(build / path).lexically_normal();
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
  if (targets.empty())
    throw std::runtime_error("CMake dependency '" + r.alias +
                             "' requires target or targets");

  std::string selection =
      request.identity + ':' + r.version + ':' + r.cmake_package;
  for (const auto &component : r.components)
    selection += ':' + component;
  for (const auto &target : targets)
    selection += ':' + target;
  const auto id = source_key(selection);
  const auto directory = project.build / layout::environment / "foreign" / id;
  const auto source = directory / "probe";
  const auto build = directory / "build";
  validate_generated_path(directory);
  std::ostringstream script;
  script << "cmake_minimum_required(VERSION 3.21)\n"
            "project(ClutchDependencyProbe LANGUAGES C CXX)\n"
            "file(REMOVE \"${CMAKE_BINARY_DIR}/package-version.txt\")\n"
            "set(BUILD_TESTING OFF CACHE BOOL \"\")\n";
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
  script << "add_custom_target(clutch_dependency)\n";
  for (const auto &target : targets) {
    script << "if(NOT TARGET " << literal(target) << ")\nmessage(FATAL_ERROR "
           << literal("dependency does not export target: " + target)
           << ")\nendif()\n"
           << "get_target_property(_kind " << literal(target)
           << " TYPE)\n"
              "if(NOT _kind MATCHES "
              "\"^(STATIC_LIBRARY|SHARED_LIBRARY|INTERFACE_LIBRARY|UNKNOWN_"
              "LIBRARY)$\")\n"
              "message(FATAL_ERROR \"dependency target must be a "
              "library\")\nendif()\n"
           << "add_dependencies(clutch_dependency " << literal(target) << ")\n";
  }
  for (const auto *const language : {"c", "cpp"}) {
    const auto probe = std::string("clutch_probe_") + language;
    script << "add_executable(" << probe << " EXCLUDE_FROM_ALL " << language
           << "." << language << ")\n"
           << "target_link_libraries(" << probe << " PRIVATE";
    for (const auto &target : targets)
      script << ' ' << literal(target);
    script << ")\n";
  }
  // Generate runtime paths with evaluated target expressions. Imported DLLs
  // retain their runtime/import-library distinction; interfaces emit no file.
  script << "file(GENERATE OUTPUT "
            "\"${CMAKE_BINARY_DIR}/runtime-$<CONFIG>.txt\" CONTENT "
            "\"$<JOIN:$<TARGET_RUNTIME_DLLS:clutch_probe_cpp>,\\n>\")\n";
  write_if_changed(source / "CMakeLists.txt", script.str());
  write_if_changed(source / "c.c", "int main(void) { return 0; }\n");
  write_if_changed(source / "cpp.cpp", "int main() { return 0; }\n");
  const auto query = build / ".cmake/api/v1/query";
  write_if_changed(query / "codemodel-v2", "");
  write_if_changed(query / "cmakeFiles-v1", "");

  std::vector<std::string> command{
      cmake,
      "-S",
      source.u8string(),
      "-B",
      build.u8string(),
      "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
      "-DCMAKE_BUILD_TYPE=" +
          std::string(options.release ? "Release" : "Debug"),
      "-DCMAKE_C_COMPILER=" + tools.cc,
      "-DCMAKE_CXX_COMPILER=" + tools.cxx,
      "-DCMAKE_CXX_STANDARD=" + project.cxx_standard,
      "-DCMAKE_C_STANDARD=" + project.c_standard,
      "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
      "-DCMAKE_C_STANDARD_REQUIRED=ON",
      "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
      "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
      "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"};
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
  if (tools.msvc)
    command.push_back("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded" +
                      std::string(options.release ? "" : "Debug") +
                      (project.runtime == "dynamic" ? "DLL" : ""));
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
  if (options.offline)
    command.insert(command.end(),
                   {"-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
                    "-DFETCHCONTENT_UPDATES_DISCONNECTED=ON"});
  if (const int code = run(command, project.root))
    throw std::runtime_error("CMake configuration failed for " + r.alias +
                             " (exit " + std::to_string(code) + ")");

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

  NativeInterface result;
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
  std::set<fs::path> artifact_paths;
  for (const auto &reference : references) {
    const auto target = read_json(reply_file(reply, reference));
    const auto name = target.at("name").get<std::string>();
    if (name == "clutch_probe_c" || name == "clutch_probe_cpp") {
      const bool c = name == "clutch_probe_c";
      compile_usage(result.usage, target, c, tools.msvc);
      if (c && target.contains("link"))
        result.usage.needs_cpp =
            target.at("link").value("language", std::string{}) == "CXX";
      if (!c) {
        const bool needs_cpp = result.usage.needs_cpp;
        link_usage(result.usage, target, build, tools.msvc);
        result.usage.needs_cpp = needs_cpp;
      }
    } else
      for (const auto &artifact : target.value("artifacts", Json::array()))
        artifact_paths.insert(
            fs::absolute(build /
                         fs::u8path(artifact.at("path").get<std::string>()))
                .lexically_normal());
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
