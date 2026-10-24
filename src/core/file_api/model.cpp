/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Serialize targets, exact compiler commands, toolchains and manifest
 * inputs.
 */
#include "model.hpp"

#include "backends/backend.hpp"
#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/source_files.hpp"
#include "core/targets/requirements.hpp"
#include "core/workspace/inputs.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace clutch::file_api {
namespace {
/// Each object declares its own schema instead of borrowing the layout version.
Json object(const std::string &kind) {
  return {{"kind", kind}, {"version", schema_version()}};
}

/// Actions are argv vectors with explicit working directories, never shell
/// text.
Json action(const std::vector<std::string> &arguments,
            const fs::path &directory) {
  return {{"arguments", arguments},
          {"directory", directory.generic_u8string()}};
}

/// Target names are unique in the graph. IDs survive source-list/profile
/// changes and are never used as output filenames or executable commands.
std::string target_id(const Target &target) { return "target:" + target.name; }

/// Public foreign usage may depend on COMPILE_LANGUAGE. Derive each overview
/// from its actual argv so C/C++ definitions and system include status agree.
std::pair<Json, std::vector<std::string>>
compile_usage(const std::vector<std::string> &arguments) {
  auto includes = Json::array();
  std::vector<std::string> defines;
  for (size_t index = 1; index < arguments.size(); ++index) {
    const auto &argument = arguments[index];
    std::string include;
    bool system = false;
    if (argument == "-isystem" || argument == "-I" || argument == "/I") {
      system = argument == "-isystem";
      if (++index >= arguments.size())
        throw std::runtime_error(
            "incomplete include option in compilation command");
      include = arguments[index];
    } else if (argument.rfind("/external:I", 0) == 0) {
      include = argument.substr(std::string_view("/external:I").size());
      system = true;
    } else if (argument.rfind("-I", 0) == 0 || argument.rfind("/I", 0) == 0)
      include = argument.substr(2);
    else if (argument.rfind("-D", 0) == 0 || argument.rfind("/D", 0) == 0)
      defines = merge_defines(defines, {argument.substr(2)});
    if (!include.empty())
      includes.push_back({{"path", fs::u8path(include).generic_u8string()},
                          {"isSystem", system}});
  }
  return {std::move(includes), std::move(defines)};
}

/// Keep the structured overview and exact per-source argv together so clients
/// can navigate a target without reverse-engineering its compiler flags.
Json target_object(const Graph &graph,
                   const Options &options,
                   const size_t index,
                   const fs::path &executable) {
  const auto &target = graph.targets[index];
  const auto &project = graph.project;
  auto result = object("target");
  result.update({{"id", target_id(target)},
                 {"name", target.name},
                 {"project", target.project_name},
                 {"localName", target.local_name},
                 {"sourceDirectory", target.source_root.generic_u8string()},
                 {"type", target_model_type(target.type)},
                 {"isTest", target.is_test},
                 {"buildByDefault", target.build_by_default},
                 {"artifacts", {{{"path", target.output.generic_u8string()}}}},
                 {"dependencies", Json::array()},
                 {"directory", project.build.generic_u8string()},
                 {"sources", Json::array()},
                 {"compileGroups", Json::array()},
                 {"link", action(target.link, project.build)}});
  result["precompiledHeaders"] = Json::array();
  result["foreignDependencies"] = target.foreign_dependencies;
  result["additionalFiles"] = Json::array();
  for (const auto &path : target.additional_files)
    result["additionalFiles"].push_back(path.generic_u8string());
  result["linkInputs"] = Json::array();
  for (const auto &path : target.link_inputs)
    result["linkInputs"].push_back(path.generic_u8string());
  for (const auto &prefix : target.precompiled_headers) {
    auto headers = Json::array();
    for (const auto &header : prefix.headers)
      headers.push_back(header.generic_u8string());

    Json description = {{"language", prefix.language == "c" ? "C" : "CXX"},
                        {"headers", headers},
                        {"textHeader", prefix.ide_header.generic_u8string()},
                        {"managedByGenerator", graph.backend.name == "xcode"}};
    if (graph.backend.name == "xcode")
      description["prefixHeader"] =
          (prefix.header.parent_path().parent_path() / "xcode-prefix.h")
              .generic_u8string();
    else {
      description["artifact"] = prefix.creator.object.generic_u8string();
      description["create"] = action(prefix.creator.arguments, project.build);
    }

    result["precompiledHeaders"].push_back(std::move(description));
  }
  std::map<std::string, size_t> groups;
  for (const auto &[source, output, arguments] : target.units) {
    const std::string language = source.extension() == ".c" ? "C" : "CXX";
    const auto [group, inserted] = groups.emplace(language, groups.size());
    if (inserted) {
      const auto [includes, defines] = compile_usage(arguments);

      result["compileGroups"].push_back(
          {{"language", language},
           {"standard",
            language == "C" ? target.c_standard : target.cxx_standard},
           {"includes", includes},
           {"defines", defines}});
    }

    result["sources"].push_back({{"path", source.generic_u8string()},
                                 {"object", output.generic_u8string()},
                                 {"compileGroupIndex", group->second},
                                 {"arguments", arguments}});
  }

  for (const auto dependency : target.dependencies)
    result["dependencies"].push_back(target_id(graph.targets.at(dependency)));
  if (!target.import_library.empty())
    result["artifacts"].push_back(
        {{"path", target.import_library.generic_u8string()}});
  std::vector<std::string> build{executable.generic_u8string(),
                                 "build",
                                 "--project",
                                 project.root.generic_u8string(),
                                 "--target",
                                 target.name};
  if (options.release)
    build.emplace_back("--release");
  result["actions"] = {{"build", action(build, project.root)}};
  if (target.type == TargetType::Executable) {
    result["actions"]["run"] =
        action({target.output.generic_u8string()}, target.source_root);
    if (project.debug_symbols) {
      build[1] = "debug";
      result["actions"]["debug"] = action(build, project.root);
    }
  }
  return result;
}

/// Export resolved drivers only; unknown compiler/SDK properties stay absent.
Json toolchains_object(const Graph &graph) {
  auto result = object("toolchains");
  result["toolchains"] = Json::array();
  for (const auto &[language, compiler] :
       std::array{std::pair{"C", graph.toolchain.cc},
                  std::pair{"CXX", graph.toolchain.cxx}}) {
    if (compiler.empty())
      continue;

    // Driver style is known; vendor/version/SDK are deliberately not guessed.
    result["toolchains"].push_back(
        {{"language", language},
         {"compiler",
          {{"path", fs::u8path(compiler).generic_u8string()},
           {"driverStyle", graph.toolchain.msvc ? "msvc" : "gnu"}}}});
  }

  return result;
}
}  // namespace

Json file_signature(const fs::path &path) {
  // Public replies and private caches must fingerprint identical bytes with
  // identical padding; keep hashing and read-error handling in one place.
  return {{"algorithm", "fnv1a64"}, {"value", input_signature(path)}};
}

std::map<std::string, Json>
make_objects(const Graph &graph,
             const Options &options,
             const fs::path &executable,
             const std::string &generation_directory,
             const std::set<std::string> &kinds) {
  std::map<std::string, Json> objects;
  const auto &project = graph.project;
  if (kinds.count("codemodel")) {
    auto model = object("codemodel");
    model.update(
        {{"project", {{"name", project.name}}},
         {"paths",
          {{"source", project.root.generic_u8string()},
           {"build", project.build.generic_u8string()}}},
         {"profile", options.release ? "release" : "debug"},
         {"compilePolicy",
          {{"debugSymbols", project.debug_symbols},
           {"safety",
            {{"sanitizers", project.safety.sanitizers},
             {"lifetime", project.safety.lifetime},
             {"strictness", project.safety.strictness}}}}},
         {"generator",
          {{"name", graph.backend.name},
           {"executable",
            fs::u8path(graph.backend.executable).generic_u8string()}}},
         {"compilationDatabase",
          (project.build / layout::compilation_database).generic_u8string()},
         {"targets", Json::array()}});
    model["workspace"] = project.is_workspace;
    model["compilePolicy"]["warnings"] = project.warnings == "custom"
                                             ? Json(project.warning_options)
                                             : Json(project.warnings);
    model["projects"] = Json::array();
    for (const auto &[name, version, root] : project.projects)
      model["projects"].push_back({{"name", name},
                                   {"version", version},
                                   {"source", root.generic_u8string()}});
    for (size_t index = 0; index < graph.targets.size(); ++index) {
      const auto filename = "target-" + std::to_string(index) + ".json";
      const auto &target = graph.targets[index];
      const auto reference =
          (fs::u8path(generation_directory) / filename).generic_u8string();
      model["targets"].push_back({{"id", target_id(target)},
                                  {"name", target.name},
                                  {"jsonFile", reference}});
      objects.emplace(filename,
                      target_object(graph, options, index, executable));
    }

    std::vector<std::string> configure{executable.generic_u8string(),
                                       project.root.generic_u8string(),
                                       "--generator",
                                       graph.backend.name,
                                       "--build-tool",
                                       graph.backend.executable};
    for (const auto &[flag, compiler] :
         std::array{std::pair{"--cc", graph.toolchain.cc},
                    std::pair{"--cxx", graph.toolchain.cxx}}) {
      if (!compiler.empty())
        configure.insert(configure.end(), {flag, compiler});
    }

    std::vector<std::string> build{executable.generic_u8string(),
                                   "build",
                                   "--project",
                                   project.root.generic_u8string()};
    if (options.release) {
      configure.emplace_back("--release");
      build.emplace_back("--release");
    }
    if (options.locked)
      configure.emplace_back("--locked");
    if (options.offline)
      configure.emplace_back("--offline");
    if (options.update_dependencies)
      configure.emplace_back("--auto-update-dependencies");

    model["packages"] = Json::array();
    model["foreignBuilds"] = Json::array();
    for (const auto &native_build : project.foreign_builds) {
      auto artifacts = Json::array();
      for (const auto &artifact : native_build.artifacts)
        artifacts.push_back(artifact.generic_u8string());
      model["foreignBuilds"].push_back(
          {{"id", native_build.id},
           {"directory", native_build.directory.generic_u8string()},
           {"artifacts", artifacts},
           {"compilationDatabase",
            native_build.compilation_database.generic_u8string()}});
    }
    for (const auto &package : project.packages)
      model["packages"].push_back(
          {{"id", package.id},
           {"git", package.git},
           {"provider", package.provider},
           {"locator", package.locator},
           {"revision", package.revision},
           {"version", package.version},
           {"source", package.source.generic_u8string()},
           {"dependencies", package.dependencies}});

    model["actions"] = {{"configure", action(configure, project.root)},
                        {"build", action(build, project.root)}};
    objects.emplace("codemodel.json", std::move(model));
  }

  if (kinds.count("toolchains"))
    objects.emplace("toolchains.json", toolchains_object(graph));

  if (kinds.count("inputs")) {
    auto inputs = object("inputs");
    std::vector<std::string> extensions(source_extensions.begin(),
                                        source_extensions.end());
    extensions.insert(extensions.end(),
                      header_extensions.begin(),
                      header_extensions.end());
    inputs.update({{"configurationInputs", configuration_inputs(project)},
                   {"sourceDiscovery",
                    {{"directory", project.root.generic_u8string()},
                     {"recursive", true},
                     {"extensions", extensions},
                     {"reconfigureOnAddRemove", true}}}});
    inputs["sourceDiscoveries"] = Json::array();
    for (const auto &member : project.projects)
      inputs["sourceDiscoveries"].push_back(
          {{"project", member.name},
           {"directory", member.root.generic_u8string()},
           {"recursive", true},
           {"extensions", extensions},
           {"reconfigureOnAddRemove", true}});
    objects.emplace("inputs.json", std::move(inputs));
  }

  return objects;
}

Json configuration_inputs(const Project &project) {
  auto result = Json::array();
  for (const auto &[path, signature] : project.inputs)
    result.push_back(
        {{"path", path.generic_u8string()},
         {"signature", {{"algorithm", "fnv1a64"}, {"value", signature}}}});
  return result;
}
}  // namespace clutch::file_api
