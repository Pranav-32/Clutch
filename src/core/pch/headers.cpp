/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Validate explicit prefix headers before generating compiler actions.
 */

#include "headers.hpp"
#include <algorithm>
#include <stdexcept>

namespace clutch::pch {
namespace {
/// Missing languages are empty; explicit lists must contain real path strings.
std::vector<std::string> array(const toml::node *const node,
                               const std::string &context) {
  std::vector<std::string> result;
  if (!node)
    return result;

  const auto *const values = node->as_array();
  if (!values)
    throw std::runtime_error(context + " must be an array of header paths");

  for (const auto &value : *values) {
    if (const auto text = value.value<std::string>(); text && !text->empty())
      result.push_back(*text);
    else
      throw std::runtime_error(context + " must contain nonempty header paths");
  }

  return result;
}
}  // namespace

Headers read(const toml::node *const node, const std::string &context) {
  Headers result;
  if (!node)
    return result;

  if (node->is_array())
    result.cpp = array(node, context);
  else if (const auto *const table = node->as_table()) {
    for (const auto &[key, value] : *table) {
      (void)value;
      if (key.str() != "c" && key.str() != "cpp")
        throw std::runtime_error("unknown " + context +
                                 " language: " + std::string(key.str()));
    }

    result.c = array(table->get("c"), context + ".c");
    result.cpp = array(table->get("cpp"), context + ".cpp");
  } else
    throw std::runtime_error(context + " must be an array or a c/cpp table");

  return result;
}

std::vector<fs::path> paths(const std::vector<std::string> &headers,
                            const fs::path &root) {
  std::vector<fs::path> result;
  for (const auto &header : headers) {
    const auto path = fs::canonical(root / fs::u8path(header));
    if (!fs::is_regular_file(path) ||
        path.generic_u8string().find_first_of("\"\r\n") != std::string::npos)
      throw std::runtime_error(
          "pch must contain regular headers with include-compatible filenames");
    if (std::find(result.begin(), result.end(), path) == result.end())
      result.push_back(path);
  }

  return result;
}
}  // namespace clutch::pch
