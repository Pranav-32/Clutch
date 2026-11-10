/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Verify optional bundle layouts, exact paths and executable aliases.
 */
#include "platform/tool_discovery.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace {
/// Keep invariant checks enabled in optimized test builds.
void require(const bool condition, const char *const message) {
  if (!condition)
    throw std::runtime_error(message);
}

/// Discovery checks files without running them; fixtures need only permissions.
void executable(const clutch::fs::path &path) {
  clutch::fs::create_directories(path.parent_path());
  std::ofstream(path) << "fixture, not an executable program\n";
  clutch::fs::permissions(path,
                          clutch::fs::perms::owner_exec,
                          clutch::fs::perm_options::add);
}

/// Override one variable for this fixture process only.
void set_environment(const char *const name, const clutch::fs::path &value) {
#ifdef _WIN32
  const auto wide_name = clutch::fs::u8path(name).wstring();
  require(_wputenv_s(wide_name.c_str(), value.wstring().c_str()) == 0,
          "cannot set fixture environment");
#else
  require(setenv(name, value.c_str(), 1) == 0,
          "cannot set fixture environment");
#endif
}
}  // namespace

/// Work only inside a caller-owned fixture directory; never alter real bundles.
int main(const int argc, char **const argv) {
  try {
    require(argc == 2, "expected fixture directory");
    const auto root = clutch::fs::absolute(argv[1]);
    const auto installation = root / "CLion custom version";
#ifdef _WIN32
    constexpr auto suffix = ".exe";
#if defined(_M_ARM64) || defined(__aarch64__)
    const auto host = clutch::fs::path("win/aarch64");
#else
    const auto host = clutch::fs::path("win/x64");
#endif
#elif defined(__APPLE__)
    constexpr auto suffix = "";
#if defined(__aarch64__) || defined(__arm64__)
    const auto host = clutch::fs::path("mac/aarch64");
#else
    const auto host = clutch::fs::path("mac/x64");
#endif
#else
    constexpr auto suffix = "";
#if defined(__aarch64__)
    const auto host = clutch::fs::path("linux/aarch64");
#else
    const auto host = clutch::fs::path("linux/x64");
#endif
#endif
    for (const auto *const tool :
         {"lldb", "gdb", "clang-tidy", "clang-format", "cmake", "ninja"}) {
      const auto *const group =
          std::string(tool).rfind("clang-", 0) == 0 ? "clang" : tool;
      auto directory = installation / "bin" / group / host;
      if (std::string(tool) != "ninja")
        directory /= "bin";

      const auto path = directory / (std::string(tool) + suffix);
      executable(path);
      const auto discovered = clutch::bundle_programs(installation, tool);
      require(discovered.size() == 1 &&
                  clutch::fs::u8path(discovered.front()) == path,
              "bundle lookup lost the requested tool path");
    }

    require(clutch::bundle_programs(root / "missing", "lldb").empty(),
            "missing installation unexpectedly resolved a debugger");
    require(clutch::bundle_programs(installation, "git").empty(),
            "unknown tool received an unrelated bundle fallback");
#ifdef _WIN32
    for (const auto &[name, relative] :
         {std::pair{"MSBuild", "MSBuild/Current/Bin/MSBuild.exe"},
          std::pair{"cmake",
                    "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/"
                    "cmake.exe"},
          std::pair{
              "ninja",
              "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"},
          std::pair{"g++", "bin/mingw/bin/g++.exe"}}) {
      const auto path = root / "other bundle" / relative;
      executable(path);
      const auto found = clutch::bundle_programs(root / "other bundle", name);
      require(found.size() == 1 && clutch::fs::u8path(found.front()) == path,
              "Visual Studio or MinGW bundle layout did not resolve");
    }
#endif
    // Installation and toolset folders sort newest first by numeric runs.
    require(clutch::newer_version_name("14.51.36014", "14.9.1"),
            "MSVC toolset 14.51 was not preferred over 14.9");
    require(clutch::newer_version_name("CLion 2026.1.10", "CLion 2026.1.9"),
            "CLion 2026.1.10 was not preferred over 2026.1.9");
    require(clutch::newer_version_name("CLion 2026.1.2", "CLion 2024.3.4") &&
                !clutch::newer_version_name("CLion 2024.3.4", "CLion 2026.1.2"),
            "CLion release years were not ordered newest first");
    require(clutch::newer_version_name("2026.1.2", "2026.1"),
            "a longer version with an equal prefix was not newer");
    require(!clutch::newer_version_name("same", "same"),
            "version ordering is not irreflexive");
    // Visual Studio mixes years and major versions: 18 (2026) > 2022 (17).
    require(clutch::visual_studio_major("18") == 18 &&
                clutch::visual_studio_major("2022") == 17 &&
                clutch::visual_studio_major("2019") == 16 &&
                clutch::visual_studio_major("2017") == 15 &&
                clutch::visual_studio_major("Shared") == -1 &&
                clutch::visual_studio_major("2030") == -1,
            "Visual Studio folder names mapped to the wrong major version");
#ifdef _WIN32
    {
      // Newer MSVC toolsets come first within one installation.
      const auto vs = root / "msvc bundle";
#if defined(_M_ARM64) || defined(__aarch64__)
      constexpr auto native_bin = "bin/Hostarm64/arm64";
#else
      constexpr auto native_bin = "bin/Hostx64/x64";
#endif
      const auto older = vs / "VC/Tools/MSVC/14.9.0" / native_bin / "cl.exe";
      const auto newer =
          vs / "VC/Tools/MSVC/14.51.36014" / native_bin / "cl.exe";
      executable(older);
      executable(newer);
      const auto found = clutch::bundle_programs(vs, "cl");
      require(found.size() == 2 && clutch::fs::u8path(found[0]) == newer &&
                  clutch::fs::u8path(found[1]) == older,
              "MSVC toolsets were not discovered newest first");
    }
#endif
    {
      // PATH wins, then the explicit CLION_HOME bundle precedes any other
      // installation. PATH excludes cmake so the adjacent-Ninja rule is idle.
      const auto path_directory = root / "path only";
      const auto path_ninja = path_directory / (std::string("ninja") + suffix);
      executable(path_ninja);
      set_environment("PATH", path_directory);
      set_environment("CLION_HOME", installation);
      const auto candidates = clutch::tool_candidates("ninja");
      const auto bundled =
          installation / "bin/ninja" / host / (std::string("ninja") + suffix);
      require(candidates.size() >= 2 &&
                  clutch::fs::u8path(candidates[0]) == path_ninja &&
                  clutch::fs::u8path(candidates[1]) == bundled,
              "PATH or CLION_HOME lost candidate precedence");
      require(clutch::fs::u8path(clutch::find_tool("ninja")) == path_ninja,
              "find_tool did not prefer the PATH executable");

      // Memoized installation roots must follow a changed CLION_HOME.
      const auto second = root / "CLion second";
      const auto second_ninja =
          second / "bin/ninja" / host / (std::string("ninja") + suffix);
      executable(second_ninja);
      set_environment("CLION_HOME", second);
      const auto refreshed = clutch::tool_candidates("ninja");
      require(refreshed.size() >= 2 &&
                  clutch::fs::u8path(refreshed[1]) == second_ninja,
              "a changed CLION_HOME reused stale installation roots");
    }
#ifdef _WIN32
    {
      // Standalone toolchains follow PATH and IDE bundles.
      const auto msys = root / "msys root";
      const auto shim = msys / "ucrt64/bin/gdb.exe";
      executable(shim);
      set_environment("MSYS2_ROOT", msys);
      set_environment("CLION_HOME", installation);
      const auto directories = clutch::common_tool_directories();
      require(std::find(directories.begin(),
                        directories.end(),
                        msys / "ucrt64/bin") != directories.end(),
              "MSYS2_ROOT toolchain was not a common tool directory");
      // PATH-managed package-manager directories are never searched.
      set_environment("SCOOP", root / "scoop root");
      const auto with_scoop = clutch::common_tool_directories();
      require(std::find(with_scoop.begin(),
                        with_scoop.end(),
                        root / "scoop root/shims") == with_scoop.end(),
              "a PATH-managed package directory was searched");
      const auto candidates = clutch::tool_candidates("gdb");
      const auto bundled = installation / "bin/gdb" / host / "bin/gdb.exe";
      const auto bundle_index =
          std::find_if(candidates.begin(),
                       candidates.end(),
                       [&](const std::string &candidate) {
                         return clutch::fs::u8path(candidate) == bundled;
                       });
      const auto shim_index =
          std::find_if(candidates.begin(),
                       candidates.end(),
                       [&](const std::string &candidate) {
                         return clutch::fs::u8path(candidate) == shim;
                       });
      require(shim_index != candidates.end(),
              "a standalone toolchain executable was not discovered");
      require(bundle_index != candidates.end() && bundle_index < shim_index,
              "a standalone toolchain directory preceded an IDE bundle");
    }
#else
    {
      // PATH-managed directories are never searched behind the user's back.
      const auto directories = clutch::common_tool_directories();
      for (const auto *const managed : {"/usr/local/bin",
                                        "/opt/homebrew/bin",
                                        "/home/linuxbrew/.linuxbrew/bin"})
        require(std::find(directories.begin(),
                          directories.end(),
                          clutch::fs::path(managed)) == directories.end(),
                "a PATH-managed directory was searched");
#ifdef __APPLE__
      require(std::find(directories.begin(),
                        directories.end(),
                        clutch::fs::path("/opt/homebrew/opt/llvm/bin")) !=
                  directories.end(),
              "Homebrew's keg-only LLVM was not searched");
#endif
    }
#endif
    require(clutch::tool_candidates((root / "missing/lldb").u8string()).empty(),
            "an explicit missing path fell back to an installed tool");

    const auto alias = root / (std::string("clang++") + suffix);
    executable(alias);
    require(clutch::fs::u8path(clutch::find_tool(alias.u8string())) == alias,
            "explicit compiler driver spelling was changed");
#ifndef _WIN32
    clutch::fs::permissions(alias,
                            clutch::fs::perms::owner_exec,
                            clutch::fs::perm_options::remove);
    require(clutch::find_tool(alias.u8string()).empty(),
            "nonexecutable POSIX file was accepted as a tool");
#endif
    std::puts("tool discovery checks passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
