/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Validate explicit profile safety policy without guessing capabilities.
 */
#include "manifest.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace clutch {
SafetySettings read_safety(const toml::table &table) {
  SafetySettings result;
  for (const auto &[key, value] : table) {
    if (key == "sanitizers") {
      const auto *const array = value.as_array();
      if (!array)
        throw std::runtime_error(
            "safety.sanitizers must be an array of strings");

      constexpr std::array supported{"address", "undefined", "leak", "thread"};
      for (const auto &item : *array) {
        const auto name = item.value<std::string>();
        if (!name || std::find(supported.begin(), supported.end(), *name) ==
                         supported.end())
          throw std::runtime_error(
              "safety.sanitizers supports address, undefined, leak and thread");

        if (std::find(result.sanitizers.begin(),
                      result.sanitizers.end(),
                      *name) != result.sanitizers.end())
          throw std::runtime_error("duplicate safety sanitizer: " + *name);

        result.sanitizers.push_back(*name);
      }
    } else if (key == "lifetime") {
      if (const auto *const setting = value.as_boolean())
        result.lifetime = setting->get();
      else
        throw std::runtime_error("safety.lifetime must be a boolean");
    } else if (key == "strictness") {
      const auto setting = value.value<std::string>();
      if (!setting || (*setting != "normal" && *setting != "strict"))
        throw std::runtime_error("safety.strictness must be normal or strict");

      result.strictness = *setting;
    } else {
      throw std::runtime_error("unknown safety field: " +
                               std::string(key.str()));
    }
  }

  const auto has = [&](const char *const name) {
    return std::find(result.sanitizers.begin(),
                     result.sanitizers.end(),
                     name) != result.sanitizers.end();
  };
  if (has("thread") && (has("address") || has("leak")))
    throw std::runtime_error("thread sanitizer cannot be combined with address "
                             "or leak; use a separate profile build");

  return result;
}
}  // namespace clutch
