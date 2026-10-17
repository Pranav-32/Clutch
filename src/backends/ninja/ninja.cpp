/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Discover bundled Ninja tools and generate rules from the resolved
 * graph, including response files and regeneration.
 */

#ifdef _MSC_VER
// Microsoft's CRT opt-out requires this reserved macro spelling.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ninja.hpp"

#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace clutch {
namespace {
#ifdef _WIN32
// Enumerate only known installation levels. Missing or unreadable optional
// installation folders must not prevent discovery of another installed tool.
std::vector<fs::path> installation_directories(const fs::path &root) {
  std::vector<fs::path> result;
  std::error_code error;
  fs::directory_iterator current(root,
                                 fs::directory_options::skip_permission_denied,
                                 error);
  const fs::directory_iterator end;
  for (; !error && current != end; current.increment(error)) {
    if (std::error_code entry_error; current->is_directory(entry_error))
      result.push_back(current->path());
  }

  // A stable order avoids changing tools between identical configure runs.
  std::sort(result.begin(), result.end());
  return result;
}

/// Return a Ninja executable from one known bundle location, or an empty
/// string.
std::string bundled_ninja(const fs::path &directory) {
  const auto executable = directory / "ninja.exe";
  if (std::error_code error; fs::is_regular_file(executable, error))
    return fs::absolute(executable).string();

  return {};
}
#endif

/// Escape Ninja variable expansion separately from shell/response-file quoting.
std::string ninja_value(const std::string &value) {
  std::string result;
  for (const char c : value) {
    if (c == '\n' || c == '\r' || c == '\0')
      throw std::runtime_error(
          "newlines and NUL bytes are not allowed in build arguments");

    if (c == '$')
      result += '$';

    result += c;
  }

  return result;
}

/// Encode a path token using Ninja's dollar escapes for spaces and separators.
std::string path(const fs::path &p) {
  std::string result;
  for (const char c : p.generic_string()) {
    if (c == '\n' || c == '\r')
      throw std::runtime_error("newlines are not supported in paths");

    if (c == '$' || c == ' ' || c == ':')
      result += '$';

    result += c;
  }

  return result;
}

// GNU drivers treat backslashes as escapes; MSVC uses Windows quoting rules.
std::string response(const std::vector<std::string> &args, const bool msvc) {
  std::string out;
  for (size_t i = 1; i < args.size(); ++i) {
    if (!msvc) {
      out += '"';
      for (const char c : args[i]) {
        if (c == '\\' || c == '"')
          out += '\\';

        out += c;
      }

      out += "\" ";
      continue;
    }

    out += '"';
    size_t slashes = 0;
    for (const char c : args[i]) {
      if (c == '\\') {
        ++slashes;
        continue;
      }

      out.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
      slashes = 0;
      out += c;
    }

    out.append(slashes * 2, '\\');
    out += "\" ";
  }

  return out;
}
} // namespace

/// Probe Windows IDE bundles only after normal executor resolution has failed.
std::string discover_ninja() {
#ifdef _WIN32
  // Visual Studio installs CMake and Ninja in adjacent package directories.
  const fs::path visual_studio_ninja =
      "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja";
  if (const auto *installation = std::getenv("VSINSTALLDIR")) {
    if (const auto executable =
          bundled_ninja(fs::path(installation) / visual_studio_ninja);
      !executable.empty())
      return executable;
  }

  if (const auto cmake = find_program("cmake"); !cmake.empty()) {
    const auto directory = fs::path(cmake).parent_path();
    for (const auto &candidate :
         {directory, directory.parent_path().parent_path() / "Ninja"}) {
      if (const auto executable = bundled_ninja(candidate); !executable.empty())
        return executable;
    }
  }

  // Default Windows installation roots cover standalone Build Tools and IDE
  // packages without pinning a product version, edition or installation drive.
  constexpr std::array installation_roots{"ProgramFiles", "ProgramFiles(x86)"};
  constexpr std::array clion_ninja_paths{"bin/ninja/win/x64",
                                         "bin/ninja/win/aarch64"};
  for (const auto *variable : installation_roots) {
    if (const auto *root = std::getenv(variable)) {
      for (const auto &version : installation_directories(
               fs::path(root) / "Microsoft Visual Studio")) {
        for (const auto &edition : installation_directories(version)) {
          if (const auto executable =
                bundled_ninja(edition / visual_studio_ninja);
            !executable.empty())
            return executable;
        }
      }

      for (const auto &installation :
           installation_directories(fs::path(root) / "JetBrains")) {
        if (installation.filename().string().rfind("CLion", 0) != 0)
          continue;

        for (const auto *relative : clion_ninja_paths) {
          if (const auto executable = bundled_ninja(installation / relative);
            !executable.empty())
            return executable;
        }
      }
    }
  }
#endif
  return {};
}

/// Write compile/link rules, dependency tracking and manifest regeneration
/// edges.
void generate_ninja(const Graph &graph,
                    const Options &options,
                    const fs::path &executable) {
  const auto &p = graph.project;
  const auto &cc = graph.toolchain.cc;
  const auto &cxx = graph.toolchain.cxx;
  std::ostringstream ninja;
  ninja
      << "# Generated by Clutch. Do not edit.\nninja_required_version = 1.10\n";
  // Response files preserve long argument lists; each driver supplies header
  // deps.
  ninja << "rule compile\n  command = $compiler @$out.rsp\n  rspfile = "
      "$out.rsp\n  "
      "rspfile_content = $args\n  description = Compiling $in\n";
  if (graph.toolchain.msvc)
    ninja << "  deps = msvc\n";
  else
    ninja << "  deps = gcc\n  depfile = $out.d\n";

  ninja << "rule link\n  command = $compiler @$out.rsp\n  rspfile = $out.rsp\n "
      " rspfile_content "
      "= $args\n  description = Linking $out\n";

  // Every application/test unit keeps its graph-selected compiler and object
  // path.
  for (const auto &target : graph.targets) {
    fs::create_directories(target.output.parent_path());

    for (const auto &[source, object, arguments] : target.units) {
      fs::create_directories(object.parent_path());
      ninja << "build " << path(object) << ": compile " << path(source)
          << "\n  compiler = "
          << ninja_value(command_line({arguments.front()})) << "\n  args = "
          << ninja_value(response(arguments, graph.toolchain.msvc)) << "\n";
    }

    ninja << "build " << path(target.output) << ": link";
    for (const auto &unit : target.units)
      ninja << ' ' << path(unit.object);

    ninja << "\n  compiler = "
        << ninja_value(command_line({target.link.front()})) << "\n  args = "
        << ninja_value(response(target.link, graph.toolchain.msvc)) << "\n";
  }

  // Tests are opt-in; the default goal below remains the application
  // executable.
  ninja << "build clutch-tests: phony";
  for (size_t i = 1; i < graph.targets.size(); ++i)
    ninja << ' ' << path(graph.targets[i].output);

  ninja << "\n";

  // Direct Ninja builds can refresh a changed manifest using these exact tools.
  // Source-list discovery still requires an explicit configuration pass.
  std::vector<std::string> regenerate{executable.string(),
                                      p.root.string(),
                                      "--build-tool",
                                      graph.backend.executable,
                                      "--generator",
                                      p.generator};
  if (!graph.toolchain.cxx.empty())
    regenerate.insert(regenerate.end(), {"--cxx", cxx});

  if (!graph.toolchain.cc.empty())
    regenerate.insert(regenerate.end(), {"--cc", cc});

  if (options.release)
    regenerate.emplace_back("--release");

  ninja << "rule regenerate\n  command = "
      << ninja_value(command_line(regenerate))
      << "\n  generator = 1\n  restat = 1\n  description = Configuring "
      << p.name << "\n";
  ninja << "build build.ninja: regenerate " << path(p.root / layout::manifest)
      << "\n";
  ninja << "default " << path(p.output) << "\n";

  write_if_changed(p.build / "build.ninja", ninja.str());
}
} // namespace clutch