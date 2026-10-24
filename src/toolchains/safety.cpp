/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Require real compiler and sanitizer runtime support before generation.
 */
#include "safety.hpp"

#include "core/options.hpp"
#include "core/project.hpp"
#include "platform/capture.hpp"
#include "platform/files.hpp"
#include "toolchain.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace clutch {
namespace {
/// One driver option selects exactly the requested detectors, without presets.
std::string sanitizer_flag(const Project &project, const Toolchain &tools) {
  if (project.safety.sanitizers.empty())
    return {};

  if (tools.msvc) {
    if (project.safety.sanitizers != std::vector<std::string>{"address"})
      throw std::runtime_error("Microsoft-style safety currently supports only "
                               "the address sanitizer");

    return "/fsanitize=address";
  }

  std::string flag = "-fsanitize=";
  for (const auto &name : project.safety.sanitizers) {
    if (flag.back() != '=')
      flag += ',';

    flag += name;
  }
  return flag;
}
}  // namespace

std::vector<std::string> safety_compile_flags(const Project &project,
                                              const Toolchain &tools,
                                              const bool c) {
  std::vector<std::string> flags;
  if (const auto sanitize = sanitizer_flag(project, tools); !sanitize.empty()) {
    flags.push_back(sanitize);
    if (!tools.msvc) {
      flags.emplace_back("-fno-omit-frame-pointer");
      if (project.safety.strictness == "strict")
        flags.emplace_back("-fno-sanitize-recover=all");
    }
  }

  if (project.safety.lifetime && !c) {
    const auto prefix = tools.msvc ? std::string("/clang:") : std::string{};
    const auto strict = project.safety.strictness == "strict";
    flags.push_back(prefix + (strict ? "-Wlifetime-safety"
                                     : "-Wlifetime-safety-permissive"));
    if (strict)
      flags.push_back(prefix + "-Werror=lifetime-safety");
  }
  return flags;
}

std::vector<std::string> safety_link_flags(const Project &project,
                                           const Toolchain &tools) {
  if (const auto flag = sanitizer_flag(project, tools); !flag.empty())
    return {flag};

  return {};
}

void verify_safety(Project &project,
                   const Toolchain &tools,
                   const Options &options) {
  if (project.safety.sanitizers.empty() && !project.safety.lifetime)
    return;

  if (project.safety.lifetime && tools.cxx.empty())
    throw std::runtime_error("safety.lifetime requires C++ sources and a "
                             "compiler with Clang lifetime analysis");

  const auto directory = project.build / ".clutch" / "probes" / "safety";
  validate_generated_path(directory);
  for (const auto c : {true, false}) {
    const auto &compiler = c ? tools.cc : tools.cxx;
    if (compiler.empty())
      continue;

    const auto flags = safety_compile_flags(project, tools, c);
    if (flags.empty())
      continue;

    const auto input = directory / (c ? "probe.c" : "probe.cpp");
    const auto output = directory / (c ? "probe-c.exe" : "probe-cpp.exe");
    const auto object = directory / (c ? "probe-c.obj" : "probe-cpp.obj");
    validate_generated_path(output);
    validate_generated_path(object);
    // Some drivers warn and return success for ignored options even with /WX.
    // Require frontend evidence as well as successful runtime linking.
    std::string probe;
    if (std::find(project.safety.sanitizers.begin(),
                  project.safety.sanitizers.end(),
                  "address") != project.safety.sanitizers.end()) {
      probe +=
          "#ifndef __has_feature\n#define __has_feature(x) 0\n#endif\n"
          "#if !defined(__SANITIZE_ADDRESS__) && "
          "!__has_feature(address_sanitizer)\n"
          "#error requested address instrumentation is not active\n#endif\n";
    }
    if (project.safety.lifetime && !c) {
      probe += "#ifndef __has_warning\n#define __has_warning(x) 0\n#endif\n#if "
               "!__has_warning(\"";
      probe += project.safety.strictness == "strict"
                   ? "-Wlifetime-safety"
                   : "-Wlifetime-safety-permissive";
      probe += "\")\n#error requested Clang lifetime analysis is "
               "unavailable\n#endif\n";
    }
    write_if_changed(input, probe + "int main(void) { return 0; }\n");
    std::vector<std::string> command{compiler};
    if (tools.msvc) {
      command.insert(
          command.end(),
          {"/nologo", "/Fe" + output.u8string(), "/Fo" + object.u8string()});
      // ASan emits advisory C5072 without symbols. It is valid to request
      // instrumentation without debug information, so /WX would reject a
      // supported combination. The frontend feature guards catch ignored flags.
      if (project.debug_symbols)
        command.emplace_back("/Z7");

      command.emplace_back(project.runtime == "dynamic"
                               ? (options.release ? "/MD" : "/MDd")
                               : (options.release ? "/MT" : "/MTd"));
      if (!c)
        command.emplace_back("/EHsc");
    } else {
      command.insert(command.end(), {"-Werror", "-o", output.u8string()});
      if (project.debug_symbols)
        command.emplace_back("-g");

      if (tools.microsoft_abi) {
        command.push_back(std::string("-fms-runtime-lib=") +
                          (project.runtime == "dynamic" ? "dll" : "static") +
                          (options.release ? "" : "_dbg"));
        // Match the graph's Microsoft CRT selection rather than letting Clang
        // inject a competing release/static startup default into the probe.
        const auto *const crt = project.runtime == "dynamic"
                                    ? (options.release ? "msvcrt" : "msvcrtd")
                                    : (options.release ? "libcmt" : "libcmtd");
        command.insert(command.end(),
                       {"-nostartfiles",
                        "-Xlinker",
                        std::string("/defaultlib:") + crt,
                        "-Xlinker",
                        "/defaultlib:oldnames"});
      }
    }
    command.insert(command.end(), flags.begin(), flags.end());
    command.push_back(input.u8string());
    if (tools.msvc)
      command.insert(command.end(), {"/link", "/INCREMENTAL:NO"});

    const auto result = capture(command, directory);
    if (result.status != 0)
      throw std::runtime_error("requested profile safety is unavailable for " +
                               compiler +
                               "; install the required compiler/runtime or "
                               "change the safety settings\n" +
                               result.text);
  }

#ifdef _WIN32
  // Linking a Windows sanitizer does not put its DLL on the loader's PATH.
  // Retain matching compiler-owned runtimes in the ordinary DLL staging graph.
  if (std::find(project.safety.sanitizers.begin(),
                project.safety.sanitizers.end(),
                "address") == project.safety.sanitizers.end())
    return;

  std::set<fs::path> libraries;
  std::set<std::string> compilers{tools.cc, tools.cxx};
  compilers.erase("");
  const auto trimmed = [](std::string text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())))
      text.pop_back();
    return text;
  };
  for (const auto &compiler : compilers) {
    auto name = fs::u8path(compiler).stem().u8string();
    std::transform(name.begin(),
                   name.end(),
                   name.begin(),
                   [](const unsigned char ch) {
                     return static_cast<char>(std::tolower(ch));
                   });
    bool found = false;
    if (name == "cl") {
      // MSVC distributes current and legacy debug ASan DLLs beside its target
      // compiler. This directory is already specific to the selected target
      // ABI.
      for (const auto &entry :
           fs::directory_iterator(fs::u8path(compiler).parent_path())) {
        const auto filename = entry.path().filename().u8string();
        if (entry.is_regular_file() && entry.path().extension() == ".dll" &&
            filename.rfind("clang_rt.asan", 0) == 0 &&
            filename.find("dynamic") != filename.npos) {
          libraries.insert(fs::canonical(entry.path()));
          found = true;
        }
      }
    } else {
      const auto triple =
          capture({compiler, "--print-target-triple"}, project.root);
      if (triple.status == 0) {
        auto architecture =
            trimmed(triple.text).substr(0, triple.text.find('-'));
        if (architecture == "i686")
          architecture = "i386";

        const auto filename = "clang_rt.asan_dynamic-" + architecture + ".dll";
        const auto location =
            capture({compiler, "-print-file-name=" + filename}, project.root);
        const auto path = fs::u8path(trimmed(location.text));
        if (location.status == 0 && path.is_absolute() &&
            fs::is_regular_file(path)) {
          libraries.insert(fs::canonical(path));
          found = true;
        }
      }
    }
    if (!found)
      throw std::runtime_error(
          "cannot locate the Windows AddressSanitizer runtime for " + compiler +
          "; install its runtime beside the compiler or use a supported "
          "Clang/MSVC installation");
  }
  project.safety_runtime_libraries.assign(libraries.begin(), libraries.end());
#endif
}
}  // namespace clutch
