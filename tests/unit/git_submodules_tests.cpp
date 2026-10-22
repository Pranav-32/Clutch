/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Bound submodule traversal and preserve offline and revision contracts.
 */
#include "core/dependencies/git_submodules.hpp"
#include "platform/files.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
/// Surface an invariant failure through the test runner's common error path.
void require(const bool valid, const char *const message) {
  if (!valid)
    throw std::runtime_error(message);
}

/// A rejection must explain the intended contract, not an unrelated I/O
/// failure.
template <class Operation>
void rejects(const Operation &operation, const std::string &reason) {
  try {
    operation();
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find(reason) != std::string::npos,
            "unexpected submodule rejection");
    return;
  }
  throw std::runtime_error("invalid submodule graph was accepted");
}
}  // namespace

/// Exercise filesystem boundaries and an oversized graph without network tools.
// All exceptions are caught below. LLVM's MSVC STL analysis still reports
// bad_array_new_length from filesystem string conversion as escaping main.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(const int argc, const char *const *const argv) {
  try {
    require(argc == 2, "expected test artifact directory");
    namespace fs = clutch::fs;
    const auto directory =
        fs::absolute(argv[1]) /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(directory);
    const auto root = fs::canonical(directory);
    // The mock models repository metadata; no Git installation or network is
    // needed to exercise traversal limits before another clone is attempted.
    constexpr size_t sha1_digits = 40;
    const std::string revision(sha1_digits, '1');
    const auto entry = [&](const std::string &path) {
      auto record = "160000 commit " + revision;
      record += '\t';
      record += path;
      record += '\0';
      return record;
    };
    size_t operations = 0;
    bool modified_revision = false;
    std::string path = "missing";
    const clutch::dependencies::GitOperation git =
        [&](const fs::path &repository,
            const std::vector<std::string> &arguments) {
          ++operations;
          if (arguments.front() == "ls-tree")
            return entry(path);

          if (arguments.front() == "rev-parse")
            return arguments.back() == "--show-toplevel" ? repository.u8string()
                   : modified_revision ? std::string(sha1_digits, '2')
                                       : revision;

          if (arguments.front() == "status")
            return std::string{};

          throw std::runtime_error("offline traversal attempted a Git write");
        };
    rejects(
        [&] {
          clutch::dependencies::prepare_git_submodules(root, true, git);
        },
        "unavailable offline");
    require(operations == 1,
            "offline validation attempted additional Git operations");
    path = "../redirect";
    rejects(
        [&] {
          clutch::dependencies::prepare_git_submodules(root, true, git);
        },
        "invalid Git submodule path");

    path = "changed";
    clutch::write_if_changed(root / path / ".git", "mock repository marker\n");
    modified_revision = true;
    rejects(
        [&] {
          clutch::dependencies::prepare_git_submodules(root, true, git);
        },
        "revision was modified");
    modified_revision = false;

    // The documented maximum is 64 levels. A longer synthetic graph must fail
    // with a bounded diagnostic rather than exhausting stack/process resources.
    constexpr size_t documented_depth_limit = 64;
    constexpr auto graph_levels = documented_depth_limit + 1;
    auto nested = root;
    path = "x";
    for (size_t depth = 0; depth < graph_levels; ++depth) {
      nested /= path;
      clutch::write_if_changed(nested / ".git", "mock repository marker\n");
    }
    operations = 0;
    rejects(
        [&] {
          clutch::dependencies::prepare_git_submodules(root, true, git);
        },
        "traversal limits");
    // Leave room for metadata validation per level while rejecting runaway
    // traversal independently of the current sequence of Git commands.
    constexpr size_t git_operation_budget = 1000;
    require(operations < git_operation_budget,
            "submodule traversal failed to bound Git operations");
    std::cout << "PASS: offline validation, path containment and bounded "
                 "submodule traversal\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "submodule test failed: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "submodule test failed: unexpected exception\n";
    return 1;
  }
}
