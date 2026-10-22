/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Validate CMake reply references and retain evaluated target artifacts.
 */
#include "cmake_model.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace clutch::providers {
namespace {
// File API replies are metadata, rather than unbounded compiler output.
constexpr auto maximum_metadata_bytes = 16U * 1024U * 1024U;
constexpr auto maximum_project_targets = 16384U;

/// Reply references are filenames within the CMake-owned reply directory.
fs::path reference(const fs::path &reply, const nlohmann::json &object) {
  const auto name = fs::u8path(object.at("jsonFile").get<std::string>());
  if (name.empty() || name.has_parent_path() || name.filename() != name)
    throw std::runtime_error("invalid CMake File API reply reference");

  return reply / name;
}
}  // namespace

nlohmann::json read_cmake_json(const fs::path &path) {
  validate_generated_path(path);
  if (!fs::is_regular_file(path) ||
      fs::file_size(path) > maximum_metadata_bytes)
    throw std::runtime_error("missing or oversized CMake metadata: " +
                             path.u8string());

  std::ifstream input(path, std::ios::binary);
  auto result = nlohmann::json::parse(input);
  if (!input || !result.is_object())
    throw std::runtime_error("invalid CMake metadata: " + path.u8string());

  return result;
}

CMakeModel read_cmake_model(const fs::path &source,
                            const fs::path &build,
                            const std::string &configuration) {
  CMakeModel model;
  model.source = fs::canonical(source);
  model.build = fs::canonical(build);
  const auto reply = model.build / ".cmake/api/v1/reply";
  if (!fs::is_directory(reply))
    throw std::runtime_error("CMake did not publish File API replies");

  // CMake specifies the lexicographically greatest index as the latest one.
  for (const auto &entry : fs::directory_iterator(reply))
    if (const auto name = entry.path().filename().u8string();
        name.rfind("index-", 0) == 0 && entry.path().extension() == ".json" &&
        entry.path() > model.index)
      model.index = entry.path();

  const auto index = read_cmake_json(model.index);
  const auto &cmake = index.at("cmake");
  model.cmake = cmake.at("paths").at("cmake").get<std::string>();
  model.ctest = cmake.at("paths").at("ctest").get<std::string>();
  model.cpack = cmake.at("paths").at("cpack").get<std::string>();
  model.generator = cmake.at("generator").at("name").get<std::string>();
  const auto &response = index.at("reply").at("client-clutch-project");
  const auto codemodel =
      read_cmake_json(reference(reply, response.at("codemodel-v2")));
  if (codemodel.at("kind") != "codemodel" ||
      codemodel.at("version").at("major") != 2 ||
      fs::canonical(
          fs::u8path(codemodel.at("paths").at("source").get<std::string>())) !=
          model.source ||
      fs::canonical(fs::u8path(
          codemodel.at("paths").at("build").get<std::string>())) != model.build)
    throw std::runtime_error("CMake File API source/build identity mismatch");

  const auto &configurations = codemodel.at("configurations");
  const nlohmann::json *selected = nullptr;
  for (const auto &candidate : configurations)
    if (candidate.at("name") == configuration ||
        (configuration.empty() &&
         (configurations.size() == 1 || candidate.at("name") == "Debug"))) {
      if (selected)
        throw std::runtime_error("ambiguous CMake configuration");

      selected = &candidate;
    }

  if (!selected)
    throw std::runtime_error(
        "CMake configuration is unavailable; configure with "
        "--configuration <exact-name>");

  model.configuration = selected->at("name").get<std::string>();
  const auto &targets = selected->at("targets");
  if (!targets.is_array() || targets.size() > maximum_project_targets)
    throw std::runtime_error("CMake project exceeds the target metadata limit");

  std::set<std::string> names;
  for (const auto &target : targets) {
    const auto detail = read_cmake_json(reference(reply, target));
    CMakeTarget entry{detail.at("name").get<std::string>(),
                      detail.at("type").get<std::string>(),
                      {},
                      {}};
    if (entry.name.empty() || !names.insert(entry.name).second ||
        detail.at("id") != target.at("id"))
      throw std::runtime_error("invalid or duplicate CMake target identity");

    for (const auto &artifact :
         detail.value("artifacts", nlohmann::json::array())) {
      const auto path = fs::u8path(artifact.at("path").get<std::string>());
      entry.artifacts.push_back(
          fs::absolute(model.build / path).lexically_normal());
      if (entry.type == "EXECUTABLE" &&
          path.filename().u8string() ==
              detail.value("nameOnDisk", std::string{})) {
        if (!entry.executable.empty())
          throw std::runtime_error("ambiguous CMake executable artifact");

        entry.executable = entry.artifacts.back();
      }
    }

    model.targets.push_back(std::move(entry));
  }

  return model;
}
}  // namespace clutch::providers
