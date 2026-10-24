/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Validate template declarations before any output path is created.
 */
#include "manifest.hpp"
#include "core/project.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace clutch {
namespace {
// Keep expansion work bounded even for an untrusted imported manifest.
constexpr auto maximum_configured_files = size_t{256};

/// Variable tokens use portable ASCII identifiers, not shell expressions.
bool identifier(const std::string &name) {
  return !name.empty() &&
         (std::isalpha(static_cast<unsigned char>(name.front())) ||
          name.front() == '_') &&
         std::all_of(name.begin(), name.end(), [](const unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
         });
}
}  // namespace

void read_configured_files(Project &project, const toml::table &document) {
  const auto *const node = document.get("configure_file");
  if (!node)
    return;

  const auto *const entries = node->as_array();
  if (!entries || entries->empty() ||
      entries->size() > maximum_configured_files)
    throw std::runtime_error(
        "configure_file must be a nonempty array of at most 256 tables");

  if (project.name == "workspace" && project.version.empty())
    throw std::runtime_error("configure_file requires a real [project]");

  std::set<fs::path> outputs;
  for (const auto &entry : *entries) {
    const auto *const table = entry.as_table();
    if (!table)
      throw std::runtime_error("configure_file must contain tables");

    for (const auto &[key, value] : *table) {
      (void)value;
      if (key != "input" && key != "output" && key != "variables" &&
          key != "git_version")
        throw std::runtime_error("unknown configure_file key: " +
                                 std::string(key.str()));
    }
    const auto input = (*table)["input"].value<std::string>();
    const auto output = (*table)["output"].value<std::string>();
    if (!input || input->empty() || !output || output->empty())
      throw std::runtime_error(
          "configure_file.input/output must be nonempty strings");

    if (input->find('\0') != std::string::npos ||
        output->find('\0') != std::string::npos)
      throw std::runtime_error(
          "configure_file paths must not contain NUL bytes");

    const auto relative = fs::u8path(*output).lexically_normal();
    if (relative.is_absolute() || relative.has_root_path() || relative == "." ||
        relative.filename().empty() ||
        std::any_of(relative.begin(), relative.end(), [](const fs::path &part) {
          return part == "..";
        }))
      throw std::runtime_error(
          "configure_file.output must stay inside its generated directory");

    ConfiguredFile file;
    file.input = fs::canonical(project.root / fs::u8path(*input));
    if (!fs::is_regular_file(file.input))
      throw std::runtime_error("configure_file.input must be a regular file");

    file.source_root = project.root;
    file.project_name = project.name;
    file.project_version = project.version;
    file.output = configured_directory(project.build, project.root) / relative;
    // Case-insensitive output aliases are also rejected by graph validation.
    auto output_key = file.output.generic_u8string();
#ifdef _WIN32
    std::transform(output_key.begin(),
                   output_key.end(),
                   output_key.begin(),
                   [](const unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
#endif
    const auto output_path_key = fs::u8path(output_key);
    for (const auto &other : outputs) {
      const auto inside = [](const fs::path &parent, const fs::path &child) {
        return std::mismatch(parent.begin(),
                             parent.end(),
                             child.begin(),
                             child.end())
                   .first == parent.end();
      };
      if (inside(output_path_key, other) || inside(other, output_path_key))
        throw std::runtime_error(
            "duplicate or conflicting configure_file.output: " + *output);
    }
    outputs.insert(output_path_key);

    if (const auto *const value = table->get("git_version")) {
      if (!value->is_boolean())
        throw std::runtime_error(
            "configure_file.git_version must be a boolean");

      file.git_version = value->as_boolean()->get();
    }
    if (const auto *const variables = table->get("variables")) {
      const auto *const values = variables->as_table();
      if (!values)
        throw std::runtime_error(
            "configure_file.variables must be a table of strings");

      for (const auto &[name, value] : *values) {
        const std::string key(name.str());
        const auto text = value.value<std::string>();
        if (!identifier(key) || key.rfind("PROJECT_", 0) == 0 || !text)
          throw std::runtime_error(
              "configure_file.variables requires string values, "
              "ASCII identifiers and no reserved PROJECT_ names");

        file.variables.emplace_back(key, *text);
      }
    }
    project.inputs.push_back({file.input, input_signature(file.input)});
    project.configured_files.push_back(std::move(file));
  }
}
}  // namespace clutch
