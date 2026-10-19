/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Check semantic precedence, range boundaries and malformed constraints.
 */
#include "core/dependencies/version.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
/// Fail with context even when release builds disable assert.
void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

/// Validate fixture text before borrowing any parsed optional value.
clutch::dependencies::Version version(const char *const text) {
  if (const auto parsed = clutch::dependencies::parse_version(text); parsed)
    return *parsed;

  throw std::runtime_error(std::string("invalid version fixture: ") + text);
}
}  // namespace

/// Exercise version semantics independently of Git or an installed compiler.
int main() {
  using clutch::dependencies::compare;
  using clutch::dependencies::parse_version;
  using clutch::dependencies::satisfies;
  try {
    for (const auto *const invalid : {"",
                                      "1",
                                      "1.2",
                                      "01.2.3",
                                      "1.2.3.4",
                                      "1.2.3-01",
                                      "1.2.3+",
                                      "1.2.3-",
                                      "18446744073709551616.0.0"})
      require(!parse_version(invalid),
              std::string("accepted invalid version: ") + invalid);

    const auto stable = version("v1.2.3+build.9");
    require(compare(stable, version("1.2.3")) == 0,
            "build metadata affects precedence");
    require(compare(version("1.2.3-999999999999999999999"),
                    version("1.2.3-1000000000000000000000")) < 0,
            "large numeric prerelease order");
    require(compare(version("1.2.3-alpha.2"), version("1.2.3-alpha.10")) < 0,
            "numeric prerelease order");
    // The canonical SemVer precedence chain exercises every identifier rule.
    const auto precedence = {"1.0.0-alpha",
                             "1.0.0-alpha.1",
                             "1.0.0-alpha.beta",
                             "1.0.0-beta",
                             "1.0.0-beta.2",
                             "1.0.0-beta.11",
                             "1.0.0-rc.1",
                             "1.0.0"};
    size_t left_index = 0;
    for (const auto *const left : precedence) {
      size_t right_index = 0;
      for (const auto *const right : precedence) {
        const auto order = compare(version(left), version(right));
        require((left_index == right_index && order == 0) ||
                    (left_index < right_index && order < 0) ||
                    (left_index > right_index && order > 0),
                "semantic prerelease precedence is inconsistent");
        ++right_index;
      }
      ++left_index;
    }
    require(
        parse_version(
            "18446744073709551615.18446744073709551615.18446744073709551615")
            .has_value(),
        "maximum unsigned version components were rejected");
    require(satisfies("1.9.0", "^1.2.3") && !satisfies("2.0.0", "^1.2.3"),
            "caret major bound");
    require(satisfies("0.2.9", "^0.2.3") && !satisfies("0.3.0", "^0.2.3"),
            "caret zero major bound");
    require(satisfies("0.0.3", "^0.0.3") && !satisfies("0.0.4", "^0.0.3"),
            "caret patch bound");
    require(satisfies("1.2.9", "~1.2.3") && !satisfies("1.3.0", "~1.2.3"),
            "tilde bound");
    require(satisfies("1.4.0", ">=1.2.0, <2.0.0") &&
                !satisfies("2.0.0", ">=1.2.0 <2.0.0"),
            "range intersection");
    require(!satisfies("1.3.0-beta", "^1.2.3") &&
                !satisfies("1.3.0-beta", "^1.2.3-alpha"),
            "implicit prerelease accepted");
    require(satisfies("1.2.3-beta", "^1.2.3-alpha"),
            "explicit prerelease rejected");
    require(!satisfies("release", "*") && !satisfies("1.0.0-beta", "*"),
            "wildcard accepts unstable tag");
    for (const auto *const invalid : {"^1",
                                      "1.x",
                                      ">= 1.0.0",
                                      "1.0.0 || 2.0.0",
                                      " ",
                                      "^18446744073709551615.0.0"}) {
      bool rejected = false;
      try {
        (void)satisfies("1.0.0", invalid);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, std::string("accepted invalid range: ") + invalid);
    }
  } catch (const std::exception &error) {
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
  } catch (...) {
    std::fputs("unexpected version test exception\n", stderr);
    return 1;
  }
  return 0;
}
