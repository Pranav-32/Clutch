/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Reject malformed or ambiguous command declarations before execution.
 */
#include "manifest.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <utility>

namespace clutch {
namespace {
// Bound manifest growth and accidental argument expansion before process setup.
constexpr auto max_commands = 256U;
constexpr auto max_arguments = 1024U;
constexpr std::array phases{"before-configure",
                            "after-configure",
                            "before-build",
                            "after-build"};

/// Reject misspelled context tokens even on hooks not selected by this run.
void validate_tokens(const std::string &value) {
  for (size_t index = 0; index < value.size();) {
    if (value[index++] != '@')
      continue;

    if (index < value.size() && value[index] == '@') {
      ++index;
      continue;
    }

    if (const auto end = value.find('@', index); end != value.npos) {
      const auto key = value.substr(index, end - index);
      if (key != "PROJECT_DIR" && key != "BUILD_DIR" && key != "PROFILE")
        throw std::runtime_error("unknown command variable: " + key);

      index = end + 1U;
    }
  }
}

/// Copy an exact TOML string; embedded NUL cannot cross a native argv boundary.
std::string text(const toml::node &node) {
  if (const auto value = node.value<std::string>();
      value && value->find('\0') == std::string::npos)
    return *value;

  throw std::runtime_error("command fields must contain strings without NUL");
}

/// Retain empty argument values while rejecting wrong array element types.
std::vector<std::string> strings(const toml::node *const node,
                                 const char *const field) {
  const auto *const array = node ? node->as_array() : nullptr;
  if (!array || array->size() > max_arguments)
    throw std::runtime_error(std::string("command.") + field +
                             " must be an array of at most 1024 strings");

  std::vector<std::string> result;
  for (const auto &entry : *array)
    result.push_back(text(entry));

  return result;
}
}  // namespace

std::vector<ProjectCommand> read_project_commands(const toml::table &document) {
  std::vector<ProjectCommand> result;
  const auto *const node = document.get("command");
  if (!node)
    return result;

  const auto *const array = node->as_array();
  if (!array || array->size() > max_commands)
    throw std::runtime_error("command must be an array of at most 256 tables "
                             "([[command]])");

  std::set<std::string> names;
  for (const auto &entry : *array) {
    const auto *const table = entry.as_table();
    if (!table)
      throw std::runtime_error("each command must be a table ([[command]])");

    for (const auto &[key, value] : *table) {
      (void)value;
      if (key != "name" && key != "argv" && key != "working_dir" &&
          key != "phases")
        throw std::runtime_error("unknown command key: " +
                                 std::string(key.str()));
    }

    ProjectCommand command;
    if (const auto *const name = table->get("name"))
      command.name = text(*name);

    if (command.name.empty() ||
        !std::all_of(command.name.begin(),
                     command.name.end(),
                     [](const unsigned char ch) {
                       return (ch >= 'a' && ch <= 'z') ||
                              (ch >= 'A' && ch <= 'Z') ||
                              (ch >= '0' && ch <= '9') || ch == '_' ||
                              ch == '-';
                     }))
      throw std::runtime_error("command.name must contain letters, digits, "
                               "'_' or '-'");

    if (!names.insert(command.name).second)
      throw std::runtime_error("duplicate command name: " + command.name);

    command.argv = strings(table->get("argv"), "argv");
    if (command.argv.empty() || command.argv.front().empty())
      throw std::runtime_error("command.argv requires a nonempty executable");

    for (const auto &argument : command.argv)
      validate_tokens(argument);

    if (const auto *const directory = table->get("working_dir")) {
      command.working_dir = text(*directory);
      if (command.working_dir.empty())
        throw std::runtime_error("command.working_dir must not be empty");
    }

    if (const auto *const hooks = table->get("phases")) {
      command.phases = strings(hooks, "phases");
      std::set<std::string> seen;
      for (const auto &phase : command.phases) {
        if (std::find(phases.begin(), phases.end(), phase) == phases.end())
          throw std::runtime_error("unknown command phase: " + phase);

        if (!seen.insert(phase).second)
          throw std::runtime_error("duplicate command phase: " + phase);
      }
    }

    validate_tokens(command.working_dir);

    result.push_back(std::move(command));
  }

  return result;
}
}  // namespace clutch
