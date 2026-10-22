/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Materialize nested Git repositories at the superproject's gitlinks.
 */

#include "git_submodules.hpp"
#include "platform/files.hpp"

#include <functional>
#include <sstream>
#include <stdexcept>

namespace clutch::dependencies {
namespace {
// Bound both stack depth and subprocess count for untrusted repository trees.
constexpr size_t maximum_submodule_depth = 64;
constexpr size_t maximum_submodules = 4096;
constexpr auto gitlink_mode = "160000";
}  // namespace

void prepare_git_submodules(const fs::path &source,
                            const bool offline,
                            const GitOperation &git) {
  size_t count = 0;
  std::function<void(const fs::path &, size_t)> visit =
      [&](const fs::path &repository, const size_t depth) {
        // NUL framing preserves whitespace and newlines in Git path names.
        std::istringstream entries(
            git(repository, {"ls-tree", "-r", "-z", "HEAD"}));
        std::string entry;
        while (std::getline(entries, entry, '\0')) {
          const auto separator = entry.find('\t');
          if (separator == entry.npos)
            throw std::runtime_error("invalid Git tree entry");

          std::istringstream header(entry.substr(0, separator));
          std::string mode, type, revision;
          header >> mode >> type >> revision;
          if (mode != gitlink_mode)
            continue;

          if (type != "commit")
            throw std::runtime_error("invalid Git submodule tree entry");

          if (depth >= maximum_submodule_depth || ++count > maximum_submodules)
            throw std::runtime_error(
                "Git submodule graph exceeds traversal limits");

          const auto relative = fs::u8path(entry.substr(separator + 1));
          if (relative.empty() || relative.is_absolute() ||
              relative != relative.lexically_normal() ||
              *relative.begin() == "..")
            throw std::runtime_error("invalid Git submodule path");

          const auto module = repository / relative;
          validate_generated_path(module);
          validate_generated_path(module / ".git");
          if (!fs::exists(module / ".git")) {
            if (fs::exists(module) &&
                (!fs::is_directory(module) || !fs::is_empty(module)))
              throw std::runtime_error(
                  "uninitialized Git submodule contains files: " +
                  module.u8string());

            if (offline)
              throw std::runtime_error(
                  "Git submodule is unavailable offline: " + module.u8string());

            // Git resolves relative URLs against the parent's origin. Never use
            // --remote or --force: branches and user modifications cannot move
            // the gitlink pin. Initialize one level so our bounds apply before
            // another nested repository is downloaded.
            git(repository,
                {"--literal-pathspecs",
                 "-c",
                 "submodule.recurse=false",
                 "submodule",
                 "update",
                 "--init",
                 "--checkout",
                 "--depth=1",
                 "--",
                 relative.generic_u8string()});
          }

          if (fs::canonical(module) != module ||
              fs::canonical(fs::u8path(
                  git(module, {"rev-parse", "--show-toplevel"}))) != module ||
              git(module, {"rev-parse", "--verify", "HEAD^{commit}"}) !=
                  revision)
            throw std::runtime_error("Git submodule revision was modified: " +
                                     module.u8string());

          visit(module, depth + 1);
          // Override ignore settings in .gitmodules and local configuration so
          // changed tracked files, nested pins and untracked files are visible.
          if (!git(module,
                   {"status",
                    "--porcelain",
                    "--untracked-files=all",
                    "--ignore-submodules=none"})
                   .empty())
            throw std::runtime_error("Git submodule checkout was modified: " +
                                     module.u8string());
        }
      };
  visit(source, 0);
}
}  // namespace clutch::dependencies
