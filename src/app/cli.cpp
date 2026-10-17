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
#include "core/options.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include <CLI11.hpp>

#include <chrono>
#include <iostream>

namespace clutch {
/// Parse one operation and preserve compiler, backend and application exit
/// codes.
int run_cli(const int argc, char *const *const argv) {
  // Positional configuration options live on the root command; execution-only
  // subcommands consume the saved profile instead of overriding its tools.
  CLI::App app{"Clutch: a small native C and C++ build system"};
  app.set_version_flag("--version", "clutch 0.1.0");
  app.require_subcommand(0, 1);
  Options options;
  std::string source_directory;
  std::string root = fs::current_path().string();
  app.add_option(
      "source",
      source_directory,
      "Configure the directory containing clutch.toml (for example .)");
  app.add_flag("--release", options.release, "Configure the release profile");
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
  auto *const create =
      app.add_subcommand("new", "Create a new project directory");
  auto *const init =
      app.add_subcommand("init",
                         "Initialize without replacing existing sources");
  std::string destination, project_name, language = "cpp", filter;
  std::vector<std::string> runtime_args;

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
  for (auto *const command : {build, execute, test, clean}) {
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

  execute->add_option("args",
                      runtime_args,
                      "Program arguments (use -- before flags)");
  test->add_option("--filter",
                   filter,
                   "Run tests whose names contain this text");
  try {
    app.parse(argc, argv);
    // Disallow ambiguous combinations before any filesystem or process action.
    const bool has_command = !app.get_subcommands().empty();
    if (has_command && !source_directory.empty())
      throw std::runtime_error(
          "use clutch <source-directory> to configure, or a subcommand alone");

    if (has_command &&
        (!options.generator.empty() || !options.cc.empty() ||
         !options.cxx.empty() || !options.build_tool_executable.empty()))
      throw std::runtime_error("compiler and generator options apply to clutch "
          "<source-directory> configuration only");

    // Discovery and scaffolding commands do not require a configured project.
    if (*generators) {
      for (const auto &[name, description, supported] : supported_generators())
        std::cout << name << "  " << description
            << (supported ? "" : " (requires another host OS)") << '\n';

      return 0;
    }

    if (*create || *init) {
      create_project(destination.empty() ? "." : destination,
                     project_name,
                     language == "c",
                     static_cast<bool>(*create));
      return 0;
    }

    options.root = fs::absolute(has_command
                                  ? root
                                  : source_directory.empty()
                                  ? "."
                                  : source_directory);
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

      if (const auto executable = current_executable(argv[0]);
        !executable.empty()) {
        const auto graph = configure(options, executable);
        status("Configured",
               graph.project.name + " [" +
               (options.release ? "release" : "debug") + "] -> " +
               graph.project.build.string());
        return 0;
      }

      throw std::runtime_error("cannot resolve Clutch executable path");
    }

    // Execution restores the validated graph and selects only requested
    // targets.
    const auto start = std::chrono::steady_clock::now();
    const auto graph = load_configuration(options);
    std::vector<size_t> selected;
    if (*test) {
      for (size_t index = 1; index < graph.targets.size(); ++index) {
        if (graph.targets[index].name.find(filter) != std::string::npos)
          selected.push_back(index);
      }

      if (selected.empty()) {
        if (!filter.empty())
          throw std::runtime_error("no tests match --filter " + filter);

        status("Finished",
               "no tests declared; add [[test]] entries to clutch.toml");
        return 0;
      }
    } else {
      selected.push_back(0);
    }

    // Build failures stop execution before launching an application or any
    // tests.
    if (const int result = run(backend_build_command(graph, options, selected),
                               graph.project.build))
      return result;

    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
    status("Finished",
           (*test ? "test executables" : graph.project.output.string()) + " (" +
           std::to_string(elapsed).substr(0, 5) + "s)");
    // Forward program arguments as separate values and run from the source
    // root.
    if (*execute) {
      std::vector invocation{graph.project.output.string()};
      invocation.insert(invocation.end(),
                        runtime_args.begin(),
                        runtime_args.end());
      status("Running", graph.project.name);
      return run(invocation, graph.project.root);
    }

    if (*test) {
      // Continue after a runtime failure so the result reports every selected
      // test.
      size_t failures = 0;
      for (const auto index : selected) {
        const auto &target = graph.targets[index];
        status("Testing", target.name);
        if (const int code =
            run({target.output.string()}, graph.project.root)) {
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
} // namespace clutch