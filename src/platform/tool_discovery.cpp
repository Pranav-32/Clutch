/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Discover native tools in bounded, version-independent IDE locations.
 */
#include "tool_discovery.hpp"
#include "platform/environment.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <mutex>

namespace clutch {
bool newer_version_name(const std::string &left, const std::string &right) {
  const auto digit = [](const char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
  };
  size_t i = 0, j = 0;
  while (i < left.size() && j < right.size()) {
    if (digit(left[i]) && digit(right[j])) {
      // Compare digit runs numerically: skip leading zeros, then length, then
      // lexically, so "18" < "2022" and "14.9" < "14.51".
      while (i < left.size() && left[i] == '0')
        ++i;
      while (j < right.size() && right[j] == '0')
        ++j;
      auto left_end = i, right_end = j;
      while (left_end < left.size() && digit(left[left_end]))
        ++left_end;
      while (right_end < right.size() && digit(right[right_end]))
        ++right_end;
      const auto left_length = left_end - i, right_length = right_end - j;
      if (left_length != right_length)
        return left_length > right_length;

      if (const auto order =
              left.compare(i, left_length, right, j, right_length);
          order != 0)
        return order > 0;

      i = left_end;
      j = right_end;
      continue;
    }

    if (left[i] != right[j])
      return static_cast<unsigned char>(left[i]) >
             static_cast<unsigned char>(right[j]);
    ++i;
    ++j;
  }
  // A longer name with an equal prefix ("2026.1.2" vs "2026.1") is newer.
  if (left.size() - i != right.size() - j)
    return left.size() - i > right.size() - j;
  // Total order for equal-by-value names (e.g. leading zeros).
  return left > right;
}

int visual_studio_major(const std::string &folder) {
  constexpr auto maximum_digits = 4U;
  if (folder.empty() || folder.size() > maximum_digits ||
      !std::all_of(folder.begin(), folder.end(), [](const char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
      }))
    return -1;

  const auto value = std::stoi(folder);
  // Year-named releases map to their product major version.
  switch (value) {
  case 2017:
    return 15;
  case 2019:
    return 16;
  case 2022:
    return 17;
  default:
    return value >= 2000 ? -1 : value;
  }
}

namespace {
/// Missing or unreadable optional folders do not invalidate another bundle.
/// Siblings are ordered newest first by natural version comparison.
std::vector<fs::path> directories(const fs::path &root) {
  std::vector<fs::path> result;
  std::error_code error;
  fs::directory_iterator entry(root,
                               fs::directory_options::skip_permission_denied,
                               error);
  const fs::directory_iterator end;
  for (; !error && entry != end; entry.increment(error)) {
    if (std::error_code item_error; entry->is_directory(item_error))
      result.push_back(entry->path());
  }

  std::sort(result.begin(),
            result.end(),
            [](const fs::path &left, const fs::path &right) {
              return newer_version_name(left.filename().u8string(),
                                        right.filename().u8string());
            });
  return result;
}

#ifdef _WIN32
/// Visual Studio folders mix release years ("2022") with major versions
/// ("18" for Visual Studio 2026). Order by major version, newest first.
std::vector<fs::path> visual_studio_versions(const fs::path &root) {
  auto result = directories(root);
  std::stable_sort(result.begin(),
                   result.end(),
                   [](const fs::path &left, const fs::path &right) {
                     return visual_studio_major(left.filename().u8string()) >
                            visual_studio_major(right.filename().u8string());
                   });
  return result;
}
#endif

/// Preserve driver aliases and deduplicate paths without resolving symlinks.
template <typename T>
void append_unique(std::vector<T> &values, const T &value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}

/// Accept only tool families for which a bundle layout is known.
std::string tool_name(const std::string &name) {
  auto result = name;
#ifdef _WIN32
  std::transform(result.begin(),
                 result.end(),
                 result.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (fs::u8path(result).extension() == ".exe")
    result = fs::u8path(result).stem().u8string();
#endif
  constexpr std::array known{
      "clang",   "clang++", "clang-cl",     "clang-format", "clang-tidy",
      "clangd",  "llvm-ar", "lldb",         "gdb",          "gcc",
      "g++",     "ar",      "mingw32-make", "make",         "gmake",
      "cmake",   "ctest",   "cpack",        "ninja",        "ninja-build",
      "msbuild", "cl",      "nmake",        "lib",          "llvm-lib"};
  return std::find(known.begin(), known.end(), result) == known.end()
             ? std::string{}
             : result;
}

/// Gather installation roots at known depths; never recurse through projects.
std::vector<fs::path> scan_installations() {
  std::vector<fs::path> result;
  for (const auto *const variable : {"CLION_HOME", "VSINSTALLDIR"})
    if (const auto value = environment_value(variable);
        value && !value->empty())
      append_unique(result, fs::absolute(fs::u8path(*value)));

  const auto clion = [&](const fs::path &parent) {
    for (const auto &path : directories(parent)) {
      auto name = path.filename().u8string();
      std::transform(name.begin(),
                     name.end(),
                     name.begin(),
                     [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                     });
      if (name.rfind("clion", 0) == 0)
        append_unique(result, path);
    }
  };
  const auto toolbox = [&](const fs::path &apps) {
    // Toolbox installations use either a direct product folder or the older
    // product/channel/build layout. Enumerate only these documented levels.
    clion(apps);
    for (const auto &product : directories(apps)) {
      auto name = product.filename().u8string();
      std::transform(name.begin(),
                     name.end(),
                     name.begin(),
                     [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                     });
      if (name.rfind("clion", 0) != 0)
        continue;

      if (std::error_code error; fs::is_directory(product / "bin", error))
        continue;

      for (const auto &channel : directories(product))
        for (const auto &build : directories(channel))
          append_unique(result, build);
    }
  };
#ifdef _WIN32
  for (const auto *const variable : {"ProgramFiles", "ProgramFiles(x86)"}) {
    if (const auto root = environment_value(variable); root && !root->empty()) {
      const auto path = fs::u8path(*root);
      for (const auto &version :
           visual_studio_versions(path / "Microsoft Visual Studio"))
        for (const auto &edition : directories(version))
          append_unique(result, edition);

      clion(path / "JetBrains");
    }
  }

  if (const auto root = environment_value("LOCALAPPDATA");
      root && !root->empty())
    toolbox(fs::u8path(*root) / "JetBrains/Toolbox/apps");
#else
  if (const auto home = environment_value("HOME"); home && !home->empty()) {
    const auto path = fs::u8path(*home);
#ifdef __APPLE__
    clion(path / "Applications");
    toolbox(path / "Library/Application Support/JetBrains/Toolbox/apps");
#else
    toolbox(path / ".local/share/JetBrains/Toolbox/apps");
#endif
  }
#ifdef __APPLE__
  clion("/Applications");
  if (const auto developer = environment_value("DEVELOPER_DIR");
      developer && !developer->empty())
    append_unique(result, fs::u8path(*developer));

  append_unique(result, fs::path("/Applications/Xcode.app/Contents/Developer"));
#else
  clion("/opt");
  clion("/opt/jetbrains");
#endif
#endif
  return result;
}

/// Environment values that determine installation roots. A changed value
/// (for example CLION_HOME set by a test or a wrapper) invalidates the memo.
std::string installation_key() {
  std::string key;
  for (const auto *const variable : {"CLION_HOME",
                                     "VSINSTALLDIR",
                                     "ProgramFiles",
                                     "ProgramFiles(x86)",
                                     "LOCALAPPDATA",
                                     "HOME",
                                     "DEVELOPER_DIR"}) {
    const auto value = environment_value(variable);
    key += value ? '=' + *value : std::string("!");
    key += '\0';
  }
  return key;
}

/// Enumerate installation roots once per process and environment. Each
/// lookup otherwise repeats the same directory listings for every tool name.
std::vector<fs::path> installations() {
  static std::mutex mutex;
  static std::string cached_key;
  static std::vector<fs::path> cached;
  static bool valid = false;
  auto key = installation_key();
  const std::lock_guard lock(mutex);
  if (!valid || key != cached_key) {
    cached = scan_installations();
    cached_key = std::move(key);
    valid = true;
  }
  return cached;
}
}  // namespace

std::vector<fs::path> common_tool_directories() {
  // Only toolchains that normally stay off PATH even when installed.
  // Directories that exist to be on PATH (/usr/local/bin, Homebrew bin, Scoop
  // shims, Chocolatey bin, WinGet links) are excluded: when absent from PATH,
  // the user or sandbox removed them deliberately, and searching them would
  // override it.
  std::vector<fs::path> result;
#ifdef _WIN32
  for (const auto *const variable : {"ProgramFiles", "ProgramW6432"})
    if (const auto root = environment_value(variable); root && !root->empty())
      append_unique(result, fs::u8path(*root) / "LLVM/bin");

  // MSYS2 environments: UCRT64 is the project's recommended default.
  for (const auto *const variable : {"MSYS2_ROOT", "SystemDrive"})
    if (const auto value = environment_value(variable);
        value && !value->empty()) {
      const auto root = std::string(variable) == "MSYS2_ROOT"
                            ? fs::u8path(*value)
                            : fs::u8path(*value + "/msys64");
      for (const auto *const environment : {"ucrt64", "clang64", "mingw64"})
        append_unique(result, root / environment / "bin");
    }
#else
#ifdef __APPLE__
  // Homebrew's LLVM is keg-only, so its tools are often absent from PATH.
  for (const auto *const prefix : {"/opt/homebrew", "/usr/local"})
    append_unique(result, fs::path(prefix) / "opt/llvm/bin");
#else
  // Distribution LLVM packages install versioned trees beside /usr/bin.
  for (const auto &version : directories("/usr/lib")) {
    const auto name = version.filename().u8string();
    if (name.rfind("llvm-", 0) == 0)
      append_unique(result, version / "bin");
  }
#endif
#endif
  return result;
}

std::vector<std::string> bundle_programs(const fs::path &installation,
                                         const std::string &name) {
  const auto normalized = tool_name(name);
  if (normalized.empty() || fs::u8path(name).has_parent_path())
    return {};

  const auto executable = normalized == "ninja-build" ? "ninja"
                          : normalized == "gmake"     ? "make"
                          : normalized == "msbuild"   ? "MSBuild"
                                                      : normalized;
  std::vector<fs::path> locations;
#ifdef _WIN32
#if defined(_M_ARM64) || defined(__aarch64__)
  constexpr auto architecture = "aarch64";
#else
  constexpr auto architecture = "x64";
#endif
  const auto host = fs::path("win") / architecture;
  // MinGW's runtime and import tools stay beside the selected driver. Do not
  // copy executables or change the process environment to make a bundle work.
  locations = {
      installation / "bin" / "lldb" / host / "bin",
      installation / "bin" / "gdb" / host / "bin",
      installation / "bin" / "clang" / host / "bin",
      installation / "bin" / "cmake" / host / "bin",
      installation / "bin" / "ninja" / host,
      installation / "bin/mingw/bin",
      installation / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin",
      installation / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja",
      installation / "VC/Tools/Llvm" / architecture / "bin",
      installation / "MSBuild/Current/Bin"};
  if (normalized == "cl" || normalized == "nmake" || normalized == "lib") {
#if defined(_M_ARM64) || defined(__aarch64__)
    constexpr auto native_bin = "bin/Hostarm64/arm64";
#else
    constexpr auto native_bin = "bin/Hostx64/x64";
#endif
    for (const auto &version : directories(installation / "VC/Tools/MSVC"))
      locations.push_back(version / native_bin);
  }
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
  constexpr auto architecture = "aarch64";
#else
  constexpr auto architecture = "x64";
#endif
  auto root = installation;
  if (std::error_code error; fs::is_directory(root / "CLion.app", error))
    root /= "CLion.app";

  if (root.extension() == ".app")
    root /= "Contents";

  const auto host = fs::path("mac") / architecture;
  locations = {root / "bin" / "lldb" / host / "bin",
               root / "bin" / "gdb" / host / "bin",
               root / "bin" / "clang" / host / "bin",
               root / "bin" / "cmake" / host / "bin",
               root / "bin" / "ninja" / host,
               root / "bin/lldb/mac/bin",
               root / "bin/cmake/mac/bin",
               root / "bin/ninja/mac",
               root / "Toolchains/XcodeDefault.xctoolchain/usr/bin"};
#else
#if defined(__aarch64__)
  constexpr auto architecture = "aarch64";
#else
  constexpr auto architecture = "x64";
#endif
  const auto host = fs::path("linux") / architecture;
  locations = {installation / "bin" / "lldb" / host / "bin",
               installation / "bin" / "gdb" / host / "bin",
               installation / "bin" / "clang" / host / "bin",
               installation / "bin" / "cmake" / host / "bin",
               installation / "bin" / "ninja" / host,
               installation / "bin/lldb/linux/bin",
               installation / "bin/cmake/linux/bin",
               installation / "bin/ninja/linux"};
#endif
  std::vector<std::string> result;
  for (const auto &directory : locations) {
    auto path = directory / executable;
#ifdef _WIN32
    path += ".exe";
#endif
    std::error_code error;
    if (!fs::is_regular_file(path, error))
      continue;

    if (const auto resolved = find_program(fs::absolute(path).u8string());
        !resolved.empty())
      append_unique(result, resolved);
  }
  return result;
}

namespace {
/// Append fallback candidates after an already performed PATH lookup.
void append_fallbacks(std::vector<std::string> &result,
                      const std::string &name) {
  if (name.empty() || fs::u8path(name).has_parent_path() ||
      tool_name(name).empty())
    return;

  // Preserve Ninja's adjacent-CMake fallback for nonstandard installations.
  if (tool_name(name) == "ninja" || tool_name(name) == "ninja-build") {
    if (const auto cmake = find_program("cmake"); !cmake.empty()) {
      const auto directory = fs::u8path(cmake).parent_path();
      for (const auto &parent :
           {directory, directory.parent_path().parent_path() / "Ninja"}) {
        auto candidate = parent / "ninja";
#ifdef _WIN32
        candidate += ".exe";
#endif
        if (const auto executable = find_program(candidate.u8string());
            !executable.empty())
          append_unique(result, executable);
      }
    }
  }

  for (const auto &root : installations())
    for (const auto &executable : bundle_programs(root, name))
      append_unique(result, executable);

  // Standalone toolchain and package-manager directories come after IDE
  // bundles, so existing bundle selections are unchanged.
  for (const auto &directory : common_tool_directories()) {
    auto path = directory / fs::u8path(name);
#ifdef _WIN32
    if (path.extension().empty())
      path += ".exe";
#endif
    std::error_code error;
    if (!fs::is_regular_file(path, error))
      continue;

    if (const auto executable = find_program(path.u8string());
        !executable.empty())
      append_unique(result, executable);
  }
}
}  // namespace

std::vector<std::string> tool_candidates(const std::string &name) {
  std::vector<std::string> result;
  if (const auto executable = find_program(name); !executable.empty())
    result.push_back(executable);

  append_fallbacks(result, name);
  return result;
}

std::string find_tool(const std::string &name) {
  if (const auto executable = find_program(name); !executable.empty())
    return executable;

  std::vector<std::string> fallbacks;
  append_fallbacks(fallbacks, name);
  return fallbacks.empty() ? std::string{} : fallbacks.front();
}
}  // namespace clutch
