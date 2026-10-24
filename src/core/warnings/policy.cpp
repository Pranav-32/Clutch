/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Keep warning-category intent separate from native compiler syntax.
 */
#include "policy.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace clutch {
namespace {
constexpr std::string_view disabled_prefix = "no-";

/// Broad compiler levels precede category overrides and are mutually exclusive.
bool is_baseline(const std::string &name) {
  return name == "default" || name == "extra" || name == "all";
}

struct Mapping {
  std::string gnu;
  std::vector<std::string> microsoft;
};

/// MSVC IDs select documented diagnostics, not exact GCC/Clang warning sets.
/// Enable at level one so explicit categories work without a broad baseline.
const std::map<std::string, Mapping> &categories() {
  static const std::map<std::string, Mapping> mappings{
      // C4456-59: hiding local variables, parameters, members and globals.
      {"shadow", {"shadow", {"4456", "4457", "4458", "4459"}}},
      // C4244/4267/4305: narrowing values, size_t conversion and truncation.
      {"conversion", {"conversion", {"4244", "4267", "4305"}}},
      // C4245/4365: signed/unsigned value conversions.
      {"sign-conversion", {"sign-conversion", {"4245", "4365"}}},
      // C4100/4101/4189/4505: unused parameters, locals and static functions.
      {"unused", {"unused", {"4100", "4101", "4189", "4505"}}},
      // C4475/4477: format modifiers and argument-type mismatches.
      {"format", {"format=2", {"4475", "4477"}}},
      // C4996: declarations marked deprecated.
      {"deprecated", {"deprecated-declarations", {"4996"}}},
      {"pedantic", {"pedantic", {}}}};
  return mappings;
}
}  // namespace

void validate_warning_names(const std::vector<std::string> &names) {
  std::set<std::string> seen;
  bool baseline = false;
  for (const auto &name : names) {
    if (!seen.insert(name).second)
      throw std::runtime_error("duplicate warning name: " + name);

    if (is_baseline(name)) {
      if (baseline)
        throw std::runtime_error("warnings array accepts at most one baseline: "
                                 "default, extra or all");

      baseline = true;
      continue;
    }

    const auto key = name.rfind(disabled_prefix, 0) == 0
                         ? name.substr(disabled_prefix.size())
                         : name;
    if (key != "error" && !categories().count(key))
      throw std::runtime_error("unknown warning name: " + name +
                               "; see the manifest warning mappings");
  }
}

std::vector<std::string> warning_flags(const std::string &preset,
                                       const std::vector<std::string> &names,
                                       const WarningDriver driver,
                                       const bool lifetime) {
  const bool microsoft = driver != WarningDriver::Gnu;
  const auto *const clang_prefix =
      driver == WarningDriver::ClangCl ? "/clang:" : "";
  if (preset != "custom") {
    if (preset == "none" && lifetime && driver != WarningDriver::Microsoft)
      return {std::string(clang_prefix) + "-Wno-everything"};

    if (microsoft)
      return {preset == "none" ? "/W0" : preset == "extra" ? "/W4" : "/W3"};

    if (preset == "none")
      return {lifetime ? "-Wno-everything" : "-w"};

    return preset == "extra" ? std::vector<std::string>{"-Wall", "-Wextra"}
                             : std::vector<std::string>{"-Wall"};
  }

  validate_warning_names(names);
  std::vector<std::string> flags;
  if (microsoft)
    flags.emplace_back("/W1");

  // Baselines precede category overrides regardless of their spelling order.
  for (const auto &name : names) {
    if (is_baseline(name)) {
      if (microsoft)
        flags.emplace_back(name == "all"     ? "/Wall"
                           : name == "extra" ? "/W4"
                                             : "/W3");
      else {
        flags.emplace_back("-Wall");
        if (name == "extra")
          flags.emplace_back("-Wextra");
      }
    }
  }

  for (const auto &name : names) {
    if (is_baseline(name))
      continue;

    const bool disable = name.rfind(disabled_prefix, 0) == 0;
    const auto key = disable ? name.substr(disabled_prefix.size()) : name;
    if (key == "error") {
      flags.emplace_back(microsoft ? (disable ? "/WX-" : "/WX")
                                   : (disable ? "-Wno-error" : "-Werror"));
      continue;
    }

    const auto &mapping = categories().at(key);
    if (driver == WarningDriver::Microsoft) {
      if (mapping.microsoft.empty())
        throw std::runtime_error(
            "warning category '" + key +
            "' has no supported Microsoft-style equivalent");

      for (const auto &code : mapping.microsoft)
        flags.push_back((disable ? "/wd" : "/w1") + code);
    } else {
      auto suffix = mapping.gnu;
      if (disable && key == "format")
        suffix = "format";

      flags.push_back(std::string(clang_prefix) + (disable ? "-Wno-" : "-W") +
                      suffix);
    }
  }

  return flags;
}
}  // namespace clutch
