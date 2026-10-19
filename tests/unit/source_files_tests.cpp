/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Check recursive matching, exclusions, malformed patterns and large
 * selections.
 */
#include "core/project_layout.hpp"
#include "core/source_files.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>

namespace {
namespace fs = clutch::fs;

/// Keep checks active in release builds and report the violated contract.
void require(const bool condition, const char *const message) {
  if (!condition)
    throw std::runtime_error(message);
}

/// Own a newly created fixture, never adopting an existing directory for
/// cleanup.
class Fixture {
  public:
  explicit Fixture(const fs::path &parent) {
    fs::create_directories(parent);
    root = fs::canonical(parent) /
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count());
    require(fs::create_directory(root), "fixture already exists");
  }

  ~Fixture() {
    try {
      clean();
    } catch (...) {
      std::fputs("source fixture cleanup failed\n", stderr);
    }
  }

  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;

  /// Report normal cleanup failures while preserving an earlier exception on
  /// unwind.
  void clean() {
    if (owned_) {
      fs::remove_all(root);
      owned_ = false;
    }
  }

  /// Write only inside the newly created fixture.
  void write(const fs::path &relative) const {
    clutch::write_if_changed(root / relative, "// source-selection fixture\n");
  }

  fs::path root;

  private:
  bool owned_ = true;
};

/// Compare complete path sets, including canonicalization, order and
/// deduplication.
void expect(const std::vector<fs::path> &actual,
            const fs::path &root,
            const std::vector<fs::path> &relative) {
  std::set<fs::path> expected;
  for (const auto &path : relative)
    expected.insert(fs::canonical(root / path));
  require(actual == std::vector(expected.begin(), expected.end()),
          "selection has missing, duplicate, unsorted or unexpected paths");
}

/// Require the intended diagnostic, rather than an unrelated filesystem
/// failure.
void rejects(const Fixture &fixture,
             const std::string &pattern,
             const std::string &diagnostic) {
  try {
    (void)clutch::expand_sources(fixture.root,
                                 {pattern},
                                 fixture.root / "src/output");
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find(diagnostic) != std::string::npos,
            "pattern failed for an unexpected reason");
    return;
  }
  throw std::runtime_error("invalid pattern was accepted: " + pattern);
}

/// Exercise filename semantics and generated-tree boundaries independently of a
/// compiler.
void unit_cases(const Fixture &fixture) {
  for (const auto *const path : {"src/main.cpp",
                                 "src/deep/worker.c",
                                 "src/deep/api.hpp",
                                 "src/notes.txt",
                                 "src/UPPER.CPP",
                                 "src/output-sibling/kept.cpp",
                                 "src/output/generated.cpp",
                                 "src/.git/hidden.cpp",
                                 "src/.clutch/deps/hidden.cpp",
                                 "src/cmake-tree/CMakeCache.txt",
                                 "src/cmake-tree/hidden.cpp",
                                 "src/owned-tree/.clutch-build-owner",
                                 "src/owned-tree/hidden.cpp"})
    fixture.write(path);
  const auto build = fixture.root / "src/output";
  const auto all = clutch::expand_sources(fixture.root, {"src/*"}, build);
  expect(all.sources,
         fixture.root,
         {"src/main.cpp", "src/deep/worker.c", "src/output-sibling/kept.cpp"});
  expect(all.additional_files,
         fixture.root,
         {"src/deep/api.hpp", "src/notes.txt", "src/UPPER.CPP"});
  const auto suffixes = clutch::expand_sources(
      fixture.root,
      {"src/*.{cpp,c,hpp}", "src/**/*.cpp", "src/./main.cpp"},
      build);
  require(suffixes.sources == all.sources,
          "overlapping patterns changed source membership");
  expect(suffixes.additional_files, fixture.root, {"src/deep/api.hpp"});
  expect(clutch::expand_sources(fixture.root, {"src/ma??.cpp"}, build).sources,
         fixture.root,
         {"src/main.cpp"});

  for (const auto *const path : {"choices/one.cpp",
                                 "choices/two.cpp",
                                 "choices/one.hpp",
                                 "choices/two.hpp"})
    fixture.write(path);
  const auto choices = clutch::expand_sources(fixture.root,
                                              {"choices/{one,two}.{cpp,hpp}"},
                                              build);
  expect(choices.sources, fixture.root, {"choices/one.cpp", "choices/two.cpp"});
  expect(choices.additional_files,
         fixture.root,
         {"choices/one.hpp", "choices/two.hpp"});

  for (const auto &[pattern, diagnostic] :
       std::vector<std::pair<std::string, std::string>>{
           {"", "empty"},
           {"src/missing*.cpp", "matched no files"},
           {"sr*/main.cpp", "filename"},
           {"src/*.{cpp,}", "empty source-pattern"},
           {"src/*.{c,{cpp,h}}", "invalid source-pattern"},
           {"src/*.cpp}", "unmatched source-pattern"}})
    rejects(fixture, pattern, diagnostic);
  require(clutch::expand_sources(fixture.root, {"missing/*.cpp"}, build, false)
              .sources.empty(),
          "optional discovery rejected an empty directory");

  // A directory alias must not bring an unrelated tree into wildcard discovery.
  fixture.write("outside/hidden.cpp");
  std::error_code error;
  fs::create_directory_symlink(fixture.root / "outside",
                               fixture.root / "src/redirect",
                               error);
  if (!error)
    require(clutch::expand_sources(fixture.root, {"src/*"}, build).sources ==
                all.sources,
            "directory link was traversed");
  else
    std::fputs("SKIP: directory symlink creation is unavailable on this host\n",
               stdout);
}

/// Many brace alternatives and repeated selections must share a bounded
/// directory scan.
void stress_cases(const Fixture &fixture) {
  constexpr auto file_count = size_t{2048}, group_count = size_t{64};
  std::set<fs::path> expected;
  for (size_t index = 0; index < file_count; ++index) {
    const auto path = fs::path("large") /
                      ("bucket" + std::to_string(index % group_count)) /
                      ("g" + std::to_string(index % group_count) + "--" +
                       std::to_string(index) + ".cpp");
    fixture.write(path);
    expected.insert(fixture.root / path);
  }
  std::string pattern = "large/g{";
  for (size_t group = 0; group < group_count; ++group)
    pattern += (group ? "," : "") + std::to_string(group);
  pattern += "}--*.cpp";
  const auto build = fixture.root / "build";
  const auto selected = clutch::expand_sources(fixture.root, {pattern}, build);
  require(selected.sources == std::vector(expected.begin(), expected.end()),
          "large brace selection lost or duplicated files");
  constexpr auto repeated_patterns = size_t{64};
  require(clutch::expand_sources(
              fixture.root,
              std::vector<std::string>(repeated_patterns, "large/**/*.cpp"),
              build)
                  .sources == selected.sources,
          "repeated patterns changed a large selection");

  // Nine binary brace groups exceed the documented 256-variant expansion bound.
  std::string excessive = "large/*";
  constexpr auto binary_groups = 9;
  for (auto group = 0; group < binary_groups; ++group)
    excessive += "{a,b}";
  rejects(fixture, excessive, "too many source-pattern alternatives");
}
}  // namespace

/// Keep the large fixture on the host's temporary filesystem rather than a
/// source bind mount.
int main(const int argc, char **const argv) {
  try {
    require(argc >= 2 && argc <= 3,
            "expected an artifact parent and optional --stress");
    const bool stress = argc == 3;
    require(!stress || std::string(argv[2]) == "--stress",
            "unknown source-test option");
    Fixture fixture(stress ? fs::temp_directory_path() / "clutch-source-stress"
                           : fs::u8path(argv[1]));
    unit_cases(fixture);
    if (stress)
      stress_cases(fixture);
    fixture.clean();
    std::fputs("PASS: source-selection contracts\n", stdout);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  } catch (...) {
    std::fputs("unexpected source-test exception\n", stderr);
    return 1;
  }
  return 0;
}
