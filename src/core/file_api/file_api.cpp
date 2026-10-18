/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Lock API writers and atomically publish complete, immutable reply
 * indices.
 */
#include "file_api.hpp"

#include "core/build_directory.hpp"
#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "core/version.hpp"
#include "model.hpp"
#include "paths.hpp"
#include "protocol.hpp"
#include "query.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace clutch::file_api {
namespace {
/// Write only new files in a private generation; never truncate an old reply.
void write_new(const fs::path &path, const Json &value) {
  if (fs::exists(path))
    throw std::runtime_error("refusing existing file API reply: " +
                             path.string());

  std::ofstream output(path, std::ios::binary);
  output << value.dump(json_indent) << '\n';
  output.close();
  if (!output)
    throw std::runtime_error("cannot write file API reply: " + path.string());
}

/// Scan immutable indices AND incomplete generations so a crash cannot cause a
/// later writer to reuse an unpublished directory or overwrite reader data.
std::string next_generation(const fs::path &reply) {
  std::uint64_t highest = 0;
  for (const auto &entry : fs::directory_iterator(reply)) {
    const auto name = entry.path().filename().u8string();
    std::string digits;
    if (name.rfind(index_prefix, 0) == 0 &&
        name.size() ==
            index_prefix.size() + generation_digits + index_suffix.size() &&
        name.substr(name.size() - index_suffix.size()) == index_suffix)
      digits = name.substr(index_prefix.size(), generation_digits);
    else if (name.rfind(generation_prefix, 0) == 0 &&
             name.size() == generation_prefix.size() + generation_digits)
      digits = name.substr(generation_prefix.size());

    if (digits.empty() ||
        digits.find_first_not_of("0123456789") != std::string::npos)
      continue;

    highest =
        std::max(highest, static_cast<std::uint64_t>(std::stoull(digits)));
  }

  if (highest == std::numeric_limits<std::uint64_t>::max())
    throw std::runtime_error("file API generation counter exhausted");

  std::ostringstream text;
  text << std::setw(generation_digits) << std::setfill('0') << highest + 1U;
  return text.str();
}

/// Fill references only on successful requests; opaque client data is
/// untouched.
void references(Json &reply, const std::string &directory) {
  if (reply.is_object() && reply.contains("kind") &&
      reply["kind"].is_string() && reply.contains("version") &&
      reply["version"].is_object() && !reply.contains("error"))
    reply["jsonFile"] =
        directory + "/" + reply["kind"].get<std::string>() + ".json";

  if (reply.is_object()) {
    for (const auto &[key, value] : reply.items()) {
      if (key != "client" && key != "requests")
        references(value, directory);
    }
  } else if (reply.is_array()) {
    for (auto &value : reply)
      references(value, directory);
  }
}
}  // namespace

struct Session::State {
  Options options;
  fs::path build, lock, directory;
  std::string generation;
  Queries queries;
  Json manifest;
  bool active = false;
  bool published = false;

  explicit State(const Options &configuration)
      : options(configuration) {}

  /// Removing our empty lock is the only deletion performed by API generation.
  ~State() {
    if (!lock.empty()) {
      std::error_code ignored;
      fs::remove(lock, ignored);
    }
  }

  Json index(const std::string &status) const {
    return {{"api", {{"major", layout_version}, {"minor", layout_minor}}},
            {"clutch", {{"version", version}}},
            {"generation", generation},
            {"status", status},
            {"profile", options.release ? "release" : "debug"},
            {"paths",
             {{"source", fs::canonical(options.root).generic_u8string()},
              {"build", build.generic_u8string()}}},
            {"reply", queries.reply},
            {"objects", Json::array()}};
  }

  void commit(const Json &value) {
    const auto relative = fs::path(api_directory) / "reply";
    const auto staged =
        relative / directory /
        ("index-" + value.at("status").get<std::string>() + ".json.tmp");
    const auto target = relative / ("index-" + generation + ".json");
    validate_path(build, staged);
    validate_path(build, target);
    write_new(build / staged, value);
    // The destination is new and on the same filesystem. Readers see either no
    // index or a complete index; old generations remain available to readers.
    fs::rename(build / staged, build / target);
    published = true;
  }
};

std::string capabilities_json() { return capabilities().dump(json_indent); }

std::string locate_json(const Options &options) {
  const auto root = fs::canonical(options.root);
  const auto build = build_directory(options);
  validate_build_directory(root, build, false);
  return Json{{"api", {{"major", layout_version}, {"minor", layout_minor}}},
              {"profile", options.release ? "release" : "debug"},
              {"paths",
               {{"source", root.generic_u8string()},
                {"build", build.generic_u8string()},
                {"query", (build / api_directory / "query").generic_u8string()},
                {"reply", (build / api_directory / "reply").generic_u8string()},
                {"compilationDatabase",
                 (build / layout::compilation_database).generic_u8string()}}}}
      .dump(json_indent);
}

Session::Session(const Options &options)
    : state_(std::make_unique<State>(options)) {}

Session::~Session() = default;

void Session::begin(const fs::path &build) {
  state_->build = build;
  const auto api = fs::path(api_directory);
  validate_path(build, api / "query");
  validate_path(build, api / "reply");
  if (!fs::exists(build / api / "query") && !fs::exists(build / api / "reply"))
    return;

  fs::create_directories(build / api);
  const auto lock = build / api / "configure.lock";
  validate_path(build, api / "configure.lock");
  if (!fs::create_directory(lock))
    throw std::runtime_error(
        "file API writer is locked; wait for configure or inspect " +
        lock.string());

  state_->lock = lock;
  fs::create_directories(build / api / "reply");
  state_->generation = next_generation(build / api / "reply");
  state_->directory = "generation-" + state_->generation;
  const auto relative = api / "reply" / state_->directory;
  validate_path(build, relative);
  if (!fs::create_directory(build / relative))
    throw std::runtime_error("cannot reserve file API generation");

  state_->active = true;
  state_->manifest =
      file_signature(fs::canonical(state_->options.root) / layout::manifest);
  state_->queries = read_queries(build);
}

void Session::publish(const Graph &graph, const fs::path &executable) {
  if (!state_->active)
    return;

  const auto directory = state_->directory.generic_string();
  const auto objects = make_objects(graph,
                                    state_->options,
                                    executable,
                                    directory,
                                    state_->queries.kinds);
  auto index = state_->index("success");
  for (const auto &[filename, object] : objects) {
    const auto relative =
        fs::path(api_directory) / "reply" / state_->directory / filename;
    validate_path(state_->build, relative);
    write_new(state_->build / relative, object);
    if (object["kind"] != "target") {
      const auto reference = (state_->directory / filename).generic_u8string();
      index["objects"].push_back({{"kind", object["kind"]},
                                  {"version", object["version"]},
                                  {"jsonFile", reference}});
    }
  }

  if (file_signature(fs::canonical(state_->options.root) / layout::manifest) !=
      state_->manifest)
    throw std::runtime_error(
        "manifest changed during file API configuration; configure again");

  index["manifest"] = state_->manifest;
  validate_inputs(graph.project.inputs);
  index["configurationInputs"] = configuration_inputs(graph.project);
  references(index["reply"], directory);
  state_->commit(index);
}

void Session::fail(const std::string &message) {
  if (!state_->active || state_->published)
    return;

  auto index = state_->index("error");
  index["error"] = {{"code", "configure-failed"},
                    {"message", diagnostic_message(message)}};
  state_->commit(index);
}
}  // namespace clutch::file_api
