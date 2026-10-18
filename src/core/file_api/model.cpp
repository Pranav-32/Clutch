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
#include "core/workspace/inputs.hpp"

#include <stdexcept>

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

/// Keep the structured overview and exact per-source argv together so clients
/// can navigate a target without reverse-engineering its compiler flags.
Json target_object(const Graph &graph,
                   const Options &options,
                   const size_t index) {
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
  std::map<std::string, size_t> groups;
  for (const auto &[source, output, arguments] : target.units) {
    const std::string language = source.extension() == ".c" ? "C" : "CXX";
    const auto [group, inserted] = groups.emplace(language, groups.size());
    if (inserted) {
      auto includes = Json::array();
      for (const auto &include : target.includes)
        includes.push_back(
            {{"path", include.generic_u8string()}, {"isSystem", false}});

      result["compileGroups"].push_back(
          {{"language", language},
           {"standard",
            language == "C" ? target.c_standard : target.cxx_standard},
           {"includes", includes},
           {"defines", target.defines}});
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
  result["actions"] = {
      {"build",
       action(backend_build_command(graph, options, {index}), project.build)}};
  if (target.type == TargetType::Executable)
    result["actions"]["run"] =
        action({target.output.generic_u8string()}, target.source_root);
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
          {{"path", fs::path(compiler).generic_u8string()},
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
         {"generator",
          {{"name", graph.backend.name},
           {"executable",
            fs::path(graph.backend.executable).generic_u8string()}}},
         {"compilationDatabase",
          (project.build / layout::compilation_database).generic_u8string()},
         {"targets", Json::array()}});
    model["workspace"] = project.is_workspace;
    model["projects"] = Json::array();
    for (const auto &[name, version, root] : project.projects)
      model["projects"].push_back({{"name", name},
                                   {"version", version},
                                   {"source", root.generic_u8string()}});
    for (size_t index = 0; index < graph.targets.size(); ++index) {
      const auto filename = "target-" + std::to_string(index) + ".json";
      const auto &target = graph.targets[index];
      const auto reference =
          (fs::path(generation_directory) / filename).generic_u8string();
      model["targets"].push_back({{"id", target_id(target)},
                                  {"name", target.name},
                                  {"jsonFile", reference}});
      objects.emplace(filename, target_object(graph, options, index));
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

    model["actions"] = {{"configure", action(configure, project.root)},
                        {"build", action(build, project.root)}};
    objects.emplace("codemodel.json", std::move(model));
  }

  if (kinds.count("toolchains"))
    objects.emplace("toolchains.json", toolchains_object(graph));

  if (kinds.count("inputs")) {
    auto inputs = object("inputs");
    inputs.update({{"configurationInputs", configuration_inputs(project)},
                   {"sourceDiscovery",
                    {{"directory", (project.root / "src").generic_u8string()},
                     {"recursive", true},
                     {"extensions", source_extensions},
                     {"reconfigureOnAddRemove", true}}}});
    inputs["sourceDiscoveries"] = Json::array();
    for (const auto &member : project.projects)
      inputs["sourceDiscoveries"].push_back(
          {{"project", member.name},
           {"directory", (member.root / "src").generic_u8string()},
           {"recursive", true},
           {"extensions", source_extensions},
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
