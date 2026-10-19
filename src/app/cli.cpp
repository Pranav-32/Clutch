/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Configure source directories and build, run or test saved build
 * graphs.
 */

#include "cli.hpp"

#include "app/workflow.hpp"
#include "backends/backend.hpp"
#include "core/configuration_store.hpp"
#include "core/configure.hpp"
#include "core/dependencies/foreign.hpp"
#include "core/file_api/file_api.hpp"
#include "core/options.hpp"
#include "core/targets/archive.hpp"
#include "core/targets/resolve.hpp"
#include "core/targets/runtime.hpp"
#include "core/version.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include <CLI11.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace clutch {
namespace {
// Show milliseconds consistently even when a build lasts hundreds of seconds.
constexpr auto elapsed_seconds_precision = 3;
}  // namespace

/// Parse one operation and preserve compiler, backend and application exit
/// codes.
int run_cli(const int argc, char *const *const argv) {
  // Positional configuration options live on the root command; execution-only
  // subcommands consume the saved profile instead of overriding its tools.
  CLI::App app{"Clutch: a small native C and C++ build system"};
  app.set_version_flag("--version", "clutch " + std::string(version));
  app.require_subcommand(0, 1);
  Options options;
  std::string source_directory;
  std::string root = fs::current_path().u8string();
  app.add_option(
      "source",
      source_directory,
      "Configure the directory containing clutch.toml (for example .)");
  app.add_flag("--release", options.release, "Configure the release profile");
  app.add_flag("--locked",
               options.locked,
               "Require an existing unchanged clutch.lock");
  app.add_flag("--offline",
               options.offline,
               "Configure without network access");
  app.add_flag("--auto-update-dependencies",
               options.update_dependencies,
               "Refresh untagged Git imports within their version constraints");
  app.add_option("--generator",
                 options.generator,
                 "Build-file generator (see generators)");
  app.add_option("--cc", options.cc, "C compiler executable (or CC)");
  app.add_option("--cxx", options.cxx, "C++ compiler executable (or CXX)");
  app.add_option("--build-tool",
                 options.build_tool_executable,
                 "Generator's build executable (or CLUTCH_BUILD_TOOL)");

  auto *const build =
      app.add_subcommand("build", "Build the saved configuration");
  auto *const execute =
      app.add_subcommand("run",
                         "Build and run; pass program arguments after --");
  auto *const test =
      app.add_subcommand("test", "Build and run declared test executables");
  auto *const clean =
      app.add_subcommand("clean",
                         "Remove the selected profile's build artifacts");
  auto *const generators =
      app.add_subcommand("generators",
                         "List implemented build-file generators");
  auto *const capabilities =
      app.add_subcommand("capabilities",
                         "Print machine-readable file-API capabilities");
  auto *const locate = app.add_subcommand(
      "locate",
      "Print project/profile metadata paths as JSON without configuring");
  auto *const targets =
      app.add_subcommand("targets", "List configured target names and kinds");
  auto *const archive =
      app.add_subcommand("__archive", "Internal static archive action")
          ->group("");
  auto *const copy_runtime =
      app.add_subcommand("__copy-runtime", "Internal DLL staging action")
          ->group("");
  std::string runtime_library;
  copy_runtime->add_option("--library", runtime_library)->required();
  auto *const create =
      app.add_subcommand("new", "Create a new project directory");
  auto *const init =
      app.add_subcommand("init",
                         "Initialize without replacing existing sources");
  std::string destination, project_name, language = "cpp", filter;
  std::vector<std::string> runtime_args, target_names;

  create->add_option("path", destination, "New directory")->required();
  init->add_option("path", destination, "Existing or new directory")
      ->default_str(".");
  for (auto *const command : {create, init}) {
    command->add_option("--name",
                        project_name,
                        "Project name (defaults to directory name)");
    command->add_option("--language", language, "c or cpp")
        ->check(CLI::IsMember({"c", "cpp"}));
  }

  // Reuse the same project/profile storage across mutually exclusive workflows.
  for (auto *const command :
       {build, execute, test, clean, locate, targets, archive, copy_runtime}) {
    command->add_option("--project",
                        root,
                        "Source directory containing clutch.toml");
    command->add_flag("--release",
                      options.release,
                      "Use the configured release profile");
  }

  for (auto *const command : {build, execute, test}) {
    command
        ->add_option("-j,--jobs",
                     options.jobs,
                     "Maximum concurrent compilation jobs")
        ->check(CLI::PositiveNumber);
  }

  for (auto *const command : {build, execute, test, archive, copy_runtime})
    command->add_option(
        "-t,--target",
        target_names,
        "Select a configured target by exact name (repeatable)");

  execute->add_option("args",
                      runtime_args,
                      "Program arguments (use -- before flags)");
  test->add_option("--filter",
                   filter,
                   "Run tests whose names contain this text");
  try {
    // CLI11 owns normalized UTF-8 arguments reconstructed from Windows' wide
    // command line; the native argv may already have lost Unicode characters.
    auto *const arguments = app.ensure_utf8(const_cast<char **>(argv));
    app.parse(argc, arguments);
    // Disallow ambiguous combinations before any filesystem or process action.
    const bool has_command = !app.get_subcommands().empty();
    if (has_command && !source_directory.empty())
      throw std::runtime_error(
          "use clutch <source-directory> to configure, or a subcommand alone");

    if (has_command &&
        (options.locked || options.offline || options.update_dependencies ||
         !options.generator.empty() || !options.cc.empty() ||
         !options.cxx.empty() || !options.build_tool_executable.empty()))
      throw std::runtime_error("compiler and generator options apply to clutch "
                               "<source-directory> configuration only");

    // Discovery and scaffolding commands do not require a configured project.
    if (*capabilities) {
      std::cout << file_api::capabilities_json() << '\n';
      return 0;
    }

    if (*generators) {
      for (const auto &[name, description, supported] : supported_generators())
        std::cout << name << "  " << description
                  << (supported ? "" : " (requires another host OS)") << '\n';

      return 0;
    }

    if (*create || *init) {
      create_project(fs::u8path(destination.empty() ? "." : destination),
                     project_name,
                     language == "c",
                     static_cast<bool>(*create));
      return 0;
    }

    options.root =
        fs::absolute(fs::u8path(has_command                ? root
                                : source_directory.empty() ? "."
                                                           : source_directory));
    if (*locate) {
      std::cout << file_api::locate_json(options) << '\n';
      return 0;
    }

    if (*clean) {
      clean_project(options);
      return 0;
    }

    // A source argument selects configuration; no arguments prints usage.
    if (!has_command) {
      if (source_directory.empty()) {
        std::cout << app.help();
        return 0;
      }

      if (const auto executable = current_executable(arguments[0]);
          !executable.empty()) {
        const auto graph = configure(options, executable);
        status("Configured",
               graph.project.name + " [" +
                   (options.release ? "release" : "debug") + "] -> " +
                   graph.project.build.u8string());
        return 0;
      }

      throw std::runtime_error("cannot resolve Clutch executable path");
    }

    // Execution restores the validated graph and selects only requested
    // targets.
    const auto start = std::chrono::steady_clock::now();
    const auto graph = load_configuration(options);
    if (*targets) {
      for (const auto &target : graph.targets)
        std::cout << target.name << "  " << target_type_name(target.type)
                  << (target.is_test            ? " (test)"
                      : target.build_by_default ? " (default)"
                                                : "")
                  << '\n';
      return 0;
    }
    const auto selected = select_targets(graph,
                                         target_names,
                                         static_cast<bool>(*test),
                                         static_cast<bool>(*execute),
                                         filter);
    if (*archive) {
      if (selected.size() != 1)
        throw std::runtime_error(
            "internal archive action requires one --target");
      return archive_target(graph, selected.front());
    }
    if (*copy_runtime) {
      if (selected.size() != 1)
        throw std::runtime_error("internal runtime copy requires one --target");
      copy_runtime_library(graph, selected.front(), runtime_library);
      return 0;
    }
    if (selected.empty()) {
      if (*test && filter.empty() && target_names.empty()) {
        status("Finished",
               "no tests declared; add [[test]] entries to clutch.toml");
        return 0;
      }
      throw std::runtime_error(
          *test ? "no tests match --filter"
                : "no default targets; select --target <name>");
    }

    // Build failures stop execution before launching an application or any
    // tests.
    if (const int result =
            dependencies::build_foreign(graph, options, selected))
      return result;

    if (const int result = run(backend_build_command(graph, options, selected),
                               graph.project.build))
      return result;

    dependencies::stage_foreign_runtime(graph, selected);

    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::ostringstream duration;
    duration << std::fixed << std::setprecision(elapsed_seconds_precision)
             << elapsed;
    status("Finished",
           (*test ? "test executables"
            : selected.size() == 1
                ? graph.targets[selected.front()].output.u8string()
                : "selected targets") +
               " (" + duration.str() + "s)");
    // Forward program arguments as separate values and run from the source
    // root.
    if (*execute) {
      const auto &target = graph.targets[selected.front()];
      std::vector invocation{target.output.u8string()};
      invocation.insert(invocation.end(),
                        runtime_args.begin(),
                        runtime_args.end());
      status("Running", target.name);
      return run(invocation, target.source_root);
    }

    if (*test) {
      // Continue after a runtime failure so the result reports every selected
      // test.
      size_t failures = 0;
      for (const auto index : selected) {
        const auto &target = graph.targets[index];
        status("Testing", target.name);
        if (const int code =
                run({target.output.u8string()}, target.source_root)) {
          ++failures;
          std::cerr << "failed: " << target.name << " (exit " << code << ")\n";
        }
      }

      status("Test result",
             std::to_string(selected.size() - failures) + " passed; " +
                 std::to_string(failures) + " failed");
      return failures ? 1 : 0;
    }
  } catch (const CLI::ParseError &error) {
    return app.exit(error);
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
}  // namespace clutch
