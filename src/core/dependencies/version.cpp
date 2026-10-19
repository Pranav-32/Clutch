/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Enforce semantic-version precedence and bounded range intersections.
 */

#include "version.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace clutch::dependencies {
namespace {
/// Decimal components are unsigned, bounded and free of leading zeroes.
bool number(const std::string_view text, std::uint64_t &value) {
  if (text.empty() || (text.size() > 1 && text.front() == '0'))
    return false;

  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

/// Prerelease and build identifiers use the SemVer ASCII alphabet.
bool identifiers(const std::string_view text,
                 std::vector<std::string> &result,
                 const bool prerelease) {
  size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('.', start);
    const auto part =
        text.substr(start,
                    end == text.npos ? text.size() - start : end - start);
    if (part.empty() ||
        !std::all_of(part.begin(), part.end(), [](const unsigned char value) {
          return (value >= '0' && value <= '9') ||
                 (value >= 'A' && value <= 'Z') ||
                 (value >= 'a' && value <= 'z') || value == '-';
        }))
      return false;

    const bool numeric =
        std::all_of(part.begin(), part.end(), [](const unsigned char value) {
          return value >= '0' && value <= '9';
        });
    if (prerelease && numeric && part.size() > 1 && part.front() == '0')
      return false;

    result.emplace_back(part);
    if (end == text.npos)
      break;

    start = end + 1;
  }

  return true;
}

/// Ranges never wrap at the maximum component value.
void increment(std::uint64_t &value) {
  if (value == std::numeric_limits<std::uint64_t>::max())
    throw std::runtime_error("version range upper bound overflows");

  ++value;
}
}  // namespace

std::optional<Version> parse_version(std::string_view text) {
  if (!text.empty() && text.front() == 'v')
    text.remove_prefix(1);
  if (const auto build = text.find('+'); build != text.npos) {
    std::vector<std::string> ignored;
    if (!identifiers(text.substr(build + 1), ignored, false))
      return {};

    text = text.substr(0, build);
  }

  Version result;
  if (const auto dash = text.find('-'); dash != text.npos) {
    if (!identifiers(text.substr(dash + 1), result.prerelease, true))
      return {};

    text = text.substr(0, dash);
  }

  size_t start = 0;
  for (auto *const component : {&result.major, &result.minor, &result.patch}) {
    const auto end = text.find('.', start);
    if (!number(
            text.substr(start,
                        end == text.npos ? text.size() - start : end - start),
            *component))
      return {};

    if ((component == &result.patch) != (end == text.npos))
      return {};

    start = end + 1;
  }

  return result;
}

int compare(const Version &left, const Version &right) {
  const auto a = std::array{left.major, left.minor, left.patch};
  const auto b = std::array{right.major, right.minor, right.patch};
  if (a != b)
    return a < b ? -1 : 1;

  if (left.prerelease.empty() || right.prerelease.empty())
    return left.prerelease.empty() == right.prerelease.empty() ? 0
           : left.prerelease.empty()                           ? 1
                                                               : -1;
  for (size_t index = 0;
       index < std::min(left.prerelease.size(), right.prerelease.size());
       ++index) {
    const auto &x = left.prerelease[index], &y = right.prerelease[index];
    if (x == y)
      continue;

    const auto numeric = [](const std::string &value) {
      return std::all_of(value.begin(),
                         value.end(),
                         [](const unsigned char character) {
                           return character >= '0' && character <= '9';
                         });
    };
    const bool xn = numeric(x), yn = numeric(y);
    if (xn != yn)
      return xn ? -1 : 1;

    if (xn && x.size() != y.size())
      return x.size() < y.size() ? -1 : 1;

    return x < y ? -1 : 1;
  }

  return left.prerelease.size() == right.prerelease.size()  ? 0
         : left.prerelease.size() < right.prerelease.size() ? -1
                                                            : 1;
}

bool satisfies(const std::string &version, const std::string &requirement) {
  if (requirement.empty())
    return true;

  if (requirement == "*") {
    const auto actual = parse_version(version);
    return actual && actual->prerelease.empty();
  }

  auto text = requirement;
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream tokens(text);
  std::string token;
  bool result = true, any = false, permits_prerelease = false;
  const auto actual = parse_version(version);
  while (tokens >> token) {
    any = true;
    const auto prefix = token.find_first_not_of("<>=^~");
    const auto operation = token.substr(0, prefix);
    const auto expected =
        parse_version(prefix == token.npos ? "" : token.substr(prefix));
    if (!expected ||
        (operation != "" && operation != "=" && operation != "==" &&
         operation != ">" && operation != ">=" && operation != "<" &&
         operation != "<=" && operation != "^" && operation != "~"))
      throw std::runtime_error("invalid version requirement: " + requirement);

    permits_prerelease =
        permits_prerelease ||
        (actual && !expected->prerelease.empty() &&
         actual->major == expected->major && actual->minor == expected->minor &&
         actual->patch == expected->patch);
    if (!actual) {
      result = false;
      continue;
    }

    const auto order = compare(*actual, *expected);
    if (operation == "^" || operation == "~") {
      auto upper = *expected;
      upper.prerelease.clear();
      if (operation == "^" && upper.major) {
        increment(upper.major);
        upper.minor = upper.patch = 0;
      } else if (operation == "~" || upper.minor) {
        increment(upper.minor);
        upper.patch = 0;
      } else
        increment(upper.patch);
      result = result && order >= 0 && compare(*actual, upper) < 0;
    } else
      result = result && (operation == ">"    ? order > 0
                          : operation == ">=" ? order >= 0
                          : operation == "<"  ? order < 0
                          : operation == "<=" ? order <= 0
                                              : order == 0);
  }

  if (!any)
    throw std::runtime_error("empty version requirement");

  return result && actual && (actual->prerelease.empty() || permits_prerelease);
}
}  // namespace clutch::dependencies
