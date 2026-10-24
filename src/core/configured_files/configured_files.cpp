/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Render bounded project templates and refresh local Git metadata
 * natively.
 */
#include "configured_files.hpp"
#include "core/build_graph.hpp"
#include "core/dependencies/dependency.hpp"
#include "core/options.hpp"
#include "platform/files.hpp"
#include "version.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

namespace clutch {
namespace {
constexpr auto maximum_template_bytes = size_t{1024} * 1024U;
constexpr auto maximum_rendered_bytes = size_t{4} * maximum_template_bytes;
constexpr auto maximum_batch_bytes = size_t{16} * maximum_template_bytes;

/// Native projects may share generated headers; serialize callback publication.
class Writer {
  public:
  explicit Writer(fs::path path)
      : path_(std::move(path)) {
    validate_generated_path(path_);
    fs::create_directories(path_.parent_path());
    constexpr auto timeout = std::chrono::seconds(10);
    constexpr auto poll_interval = std::chrono::milliseconds(50);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!fs::create_directory(path_)) {
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("configured-file writer is active; inspect " +
                                 path_.u8string() + " if interrupted");

      std::this_thread::sleep_for(poll_interval);
      validate_generated_path(path_);
    }
  }

  ~Writer() {
    std::error_code error;
    fs::remove(path_, error);
  }

  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;

  private:
  fs::path path_;
};

/// Perform a single substitution pass: values are text, never recursive code.
std::string render(const ConfiguredFile &file, const std::string &version) {
  if (fs::file_size(file.input) > maximum_template_bytes)
    throw std::runtime_error("configure_file.input exceeds size limit");

  std::ifstream input(file.input, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read template: " + file.input.u8string());

  std::string text(maximum_template_bytes + 1U, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (input.bad() ||
      static_cast<size_t>(input.gcount()) > maximum_template_bytes)
    throw std::runtime_error("cannot read bounded template input");

  text.resize(static_cast<size_t>(input.gcount()));

  std::map<std::string, std::string> values(file.variables.begin(),
                                            file.variables.end());
  values.emplace("PROJECT_NAME", file.project_name);
  values.emplace("PROJECT_VERSION", version);
  values.emplace("PROJECT_VERSION_BASE", file.project_version);
  std::string output;
  for (size_t index = 0; index < text.size();) {
    if (text[index] == '@' && index + 1 < text.size() &&
        text[index + 1] == '@') {
      output += '@';
      index += 2;
    } else if (const auto end =
                   text[index] == '@' ? text.find('@', index + 1) : text.npos;
               end != text.npos && end > index + 1 &&
               std::all_of(
                   text.begin() + static_cast<std::ptrdiff_t>(index + 1),
                   text.begin() + static_cast<std::ptrdiff_t>(end),
                   [](const unsigned char c) {
                     return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_';
                   })) {
      const auto key = text.substr(index + 1, end - index - 1);
      if (const auto value = values.find(key); value != values.end())
        output += value->second;
      else
        throw std::runtime_error("unknown template variable @" + key + "@ in " +
                                 file.input.u8string());

      index = end + 1;
    } else {
      // Doxygen commands and email addresses are ordinary template text.
      output += text[index++];
    }
    if (output.size() > maximum_rendered_bytes)
      throw std::runtime_error(
          "configured template expansion exceeds size limit");
  }
  return output;
}
}  // namespace

fs::path configured_directory(const fs::path &build, const fs::path &source) {
  return build / "generated" /
         dependencies::source_key(source.generic_u8string());
}

void refresh_configured_files(const Project &project) {
  if (project.configured_files.empty())
    return;

  const Writer writer(project.build / ".clutch/configure-files.lock");
  std::map<fs::path, std::string> identities;
  std::vector<std::pair<fs::path, std::string>> outputs;
  size_t total_bytes = 0;
  // Validate every destination and render every input before writing any file.
  for (const auto &file : project.configured_files) {
    const auto directory =
        configured_directory(project.build, file.source_root);
    const auto relative = file.output.lexically_relative(directory);
    if (relative.empty() || relative == "." || *relative.begin() == ".." ||
        !file.output.is_absolute())
      throw std::runtime_error("invalid configured-file output namespace");

    validate_generated_path(file.output);
    auto version = file.project_version;
    if (file.git_version) {
      if (const auto found = identities.find(file.source_root);
          found != identities.end())
        version = found->second;
      else
        version = identities
                      .emplace(file.source_root,
                               project_git_version(version, file.source_root))
                      .first->second;
    }
    outputs.emplace_back(file.output, render(file, version));
    total_bytes += outputs.back().second.size();
    if (total_bytes > maximum_batch_bytes)
      throw std::runtime_error("configured-file batch exceeds size limit");
  }
  for (const auto &[path, content] : outputs)
    write_if_changed(path, content);
}

void prepare_configured_files(Project &project) {
  refresh_configured_files(project);
  for (const auto &file : project.configured_files) {
    const auto directory =
        configured_directory(project.build, file.source_root);
    if (project.targets.empty() && project.root == file.source_root) {
      if (std::find(project.includes.begin(),
                    project.includes.end(),
                    directory) == project.includes.end())
        project.includes.push_back(directory);

      project.additional_files.push_back(file.output);
    }
    for (auto *const group : {&project.targets, &project.tests})
      for (auto &target : *group) {
        const auto root =
            target.source_root.empty() ? project.root : target.source_root;
        if (root != file.source_root)
          continue;

        if (std::find(target.includes.begin(),
                      target.includes.end(),
                      directory) == target.includes.end())
          target.includes.push_back(directory);

        target.additional_files.push_back(file.output);
      }
  }
}

std::vector<std::string> configured_files_command(const Graph &graph,
                                                  const Options &options,
                                                  const fs::path &executable) {
  std::vector<std::string> command{executable.u8string(),
                                   "__configure-files",
                                   "--project",
                                   graph.project.root.u8string()};
  if (options.release)
    command.emplace_back("--release");

  return command;
}
}  // namespace clutch
