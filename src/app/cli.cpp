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

#include "app/tooling.hpp"
#include "app/watch/watch.hpp"
#include "app/workflow.hpp"
#include "backends/backend.hpp"
#include "core/bundle/bundle.hpp"
#include "core/commands/commands.hpp"
#include "core/configuration_store.hpp"
#include "core/configure.hpp"
#include "core/configured_files/configured_files.hpp"
#include "core/dependencies/foreign.hpp"
#include "core/dependencies/updates.hpp"
#include "core/file_api/file_api.hpp"
#include "core/options.hpp"
#include "core/targets/archive.hpp"
#include "core/targets/resolve.hpp"
#include "core/targets/runtime.hpp"
#include "core/version.hpp"
#include "platform/arguments.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include "providers/cmake.hpp"
#include <CLI11.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <tuple>

#ifdef _MSC_VER
// CLI11's native wide-command-line normalization calls the Windows Shell API.
// Embed the SDK import for MSVC and MSVC-ABI Clang in native self-hosted
// builds.
#pragma comment(lib, "shell32.lib")
#endif

namespace clutch {
namespace {
// Show milliseconds consistently even when a build lasts hundreds of seconds.
constexpr auto elapsed_seconds_precision = 3;
// Human output abbreviates commits; the unchanged lock retains complete IDs.
constexpr auto displayed_revision_length = 12U;

/// Show version and commit together, including branch updates without a
/// version.
std::string
describe_package(const std::optional<dependencies::Package> &package) {
  if (!package)
    return "(not required)";

  return (package->version.empty() || package->version == "unversioned"
              ? package->reference
              : package->version) +
         " @ " + package->revision.substr(0, displayed_revision_length);
}

/// Present one compatible update proposal without publishing its resolution.
void report_dependency_updates(const Options &options) {
  const auto updates = dependencies::check_updates(options);
  if (updates.empty()) {
    status("Up to date", "no compatible Git dependency updates");
    return;
  }

  for (const auto &update : updates)
    status("Available",
           update.source + ": " + describe_package(update.previous) + " -> " +
               describe_package(update.available));

  std::vector<std::string> apply{"clutch",
                                 options.root.u8string(),
                                 "--auto-update-dependencies"};
  if (options.release)
    apply.emplace_back("--release");

  status("Update", "apply a fresh resolution with " + command_line(apply));
}
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
  providers::CMakeRequest cmake_request;
  std::string provider;
  std::string source_directory;
  std::string root = fs::current_path().u8string();
  app.add_option(
      "source",
      source_directory,
      "Configure the directory containing clutch.toml (for example .)");
  app.add_flag("--release", options.release, "Configure the release profile");
  app.add_flag(
      "--targets",
      options.discover_targets,
      "List project/dependency library targets without saving build rules");
  app.add_flag("--locked",
               options.locked,
               "Require an existing unchanged clutch.lock");
  app.add_flag("--offline",
               options.offline,
               "Configure without network access");
  app.add_flag("--auto-update-dependencies",
               options.update_dependencies,
               "Refresh untagged Git imports within their version constraints");
  app.add_flag("--check-updates",
               options.check_updates,
               "Report compatible dependency updates after configuring without "
               "applying them");
  app.add_option("--generator",
                 options.generator,
                 "Build-file generator (see generators)");
  app.add_option("--cc", options.cc, "C compiler executable (or CC)");
  app.add_option("--cxx", options.cxx, "C++ compiler executable (or CXX)");
  app.add_option("--build-tool",
                 options.build_tool_executable,
                 "Generator's build executable (or CLUTCH_BUILD_TOOL)");
  app.add_option("--provider", provider, "Project provider: clutch or cmake");
  app.add_option("--preset",
                 cmake_request.preset,
                 "CMake configure preset (explicit)");
  app.add_option("--configuration",
                 cmake_request.configuration,
                 "Exact CMake configuration name");
  app.add_option("--cmake-arg",
                 cmake_request.arguments,
                 "CMake setting, e.g. --cmake-arg=-DFEATURE=ON (repeatable)")
      ->expected(1)
      ->allow_extra_args(false)
      ->take_all();

  auto *const build =
      app.add_subcommand("build", "Build the saved configuration");
  auto *const execute =
      app.add_subcommand("run",
                         "Build and run; pass program arguments after --");
  auto *const test =
      app.add_subcommand("test", "Build and run declared test executables");
  auto *const format = app.add_subcommand(
      "fmt",
      "Format project sources and headers with clang-format");
  auto *const lint = app.add_subcommand(
      "lint",
      "Analyze configured translation units with clang-tidy");
  auto *const debug = app.add_subcommand(
      "debug",
      "Build and launch a native debugger for an executable");
  auto *const watch = app.add_subcommand(
      "watch",
      "Build immediately and rebuild after debounced source edits");
  WatchSettings watch_settings;
  watch->add_flag("--run",
                  watch_settings.run,
                  "Run a short-lived application after each successful build");
  watch->add_flag("--test",
                  watch_settings.test,
                  "Build and execute declared tests after each change");
  watch
      ->add_option("--interval",
                   watch_settings.interval_ms,
                   "Polling interval in milliseconds (default 250)")
      ->check(CLI::Range(1, maximum_watch_delay_ms));
  watch
      ->add_option(
          "--debounce",
          watch_settings.debounce_ms,
          "Quiet period in milliseconds before rebuilding (default 200)")
      ->check(CLI::Range(0, maximum_watch_delay_ms));
  for (const auto &[flag, values, help] :
       {std::tuple{
            "--path",
            &watch_settings.paths,
            "Additional file/directory to watch, relative to the project"},
        std::tuple{"--ignore",
                   &watch_settings.ignores,
                   "File/directory to exclude from watching"}})
    watch->add_option(flag, *values, help)
        ->expected(1)
        ->allow_extra_args(false)
        ->take_all();

  watch->add_option("args",
                    watch_settings.arguments,
                    "Program arguments for --run (use -- before flags)");
  watch
      ->add_option("-t,--target",
                   watch_settings.targets,
                   "Select a configured target (repeatable)")
      ->expected(1)
      ->allow_extra_args(false)
      ->take_all();
  auto *const command_runner = app.add_subcommand(
      "exec",
      "Run a named manifest command; pass extra arguments after --");
  std::string command_name;
  std::vector<std::string> command_arguments;
  command_runner->add_option("name", command_name, "Declared command name")
      ->required();
  command_runner->add_option("args",
                             command_arguments,
                             "Literal extra command arguments");
  std::string developer_tool, lint_checks;
  bool format_check = false;
  for (auto *const command : {format, lint, debug})
    command->add_option("--tool",
                        developer_tool,
                        "Developer tool executable path or name");

  format->add_flag("--check",
                   format_check,
                   "Report formatting differences without modifying files");
  lint->add_option("--checks",
                   lint_checks,
                   "clang-tidy check selection (also reads .clang-tidy)");
  auto *const clean =
      app.add_subcommand("clean",
                         "Remove the selected profile's build artifacts");
  auto *const install =
      app.add_subcommand("install", "Build and install a CMake project");
  auto *const package =
      app.add_subcommand("package", "Build project-defined CPack packages");
  install->add_option("--prefix", cmake_request.prefix, "Installation prefix");
  install->add_option("--component",
                      cmake_request.component,
                      "CMake install component");
  auto *const bundle = app.add_subcommand(
      "bundle",
      "Create a relocatable source ZIP with resolved source dependencies");
  std::string bundle_output;
  std::vector<std::string> bundle_exclusions;
  bundle->add_option(
      "--output",
      bundle_output,
      "New ZIP path (default .clutch/bundles/<project>-sources.zip)");
  bundle->add_option("--exclude",
                     bundle_exclusions,
                     "Omit an archive-relative file/directory (repeatable)");
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
  auto *const outdated = app.add_subcommand(
      "outdated",
      "Check compatible Git dependency updates without changing pins");
  auto *const archive =
      app.add_subcommand("__archive", "Internal static archive action")
          ->group("");
  auto *const copy_runtime =
      app.add_subcommand("__copy-runtime", "Internal DLL staging action")
          ->group("");
  auto *const foreign =
      app.add_subcommand("__foreign", "Internal dependency preparation")
          ->group("");
  auto *const foreign_runtime =
      app.add_subcommand("__foreign-runtime", "Internal dependency DLL staging")
          ->group("");
  auto *const configured_files =
      app.add_subcommand("__configure-files",
                         "Internal native template refresh")
          ->group("");
  std::string foreign_id;
  foreign->add_option("--dependency", foreign_id)->required();
  foreign->add_option("--jobs", options.jobs)->check(CLI::PositiveNumber);
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
  for (auto *const command : {build,
                              execute,
                              test,
                              format,
                              lint,
                              debug,
                              watch,
                              command_runner,
                              clean,
                              locate,
                              targets,
                              outdated,
                              archive,
                              copy_runtime,
                              foreign,
                              foreign_runtime,
                              configured_files,
                              bundle}) {
    command->add_option("--project",
                        root,
                        "Source directory containing clutch.toml");
    command->add_flag("--release",
                      options.release,
                      "Use the configured release profile");
  }

  for (auto *const command :
       {build, execute, test, clean, locate, targets, install, package}) {
    command->add_option("--provider",
                        provider,
                        "Project provider: clutch or cmake");
    command->add_option("--configuration",
                        cmake_request.configuration,
                        "Exact CMake configuration name");
  }

  for (auto *const command : {install, package}) {
    command->add_option("--project", root, "CMake source directory");
    command->add_flag("--release",
                      options.release,
                      "Use the configured release profile");
  }

  for (auto *const command : {build, test, package})
    command->add_option(
        "--preset",
        cmake_request.preset,
        "Explicit native build/test/package preset (names are independent)");

  for (auto *const command : {build, execute, test, debug, install, package}) {
    command->add_flag(
        "--clean",
        options.clean_build,
        "Recompile selected targets and dependencies before continuing");
    command
        ->add_option("-j,--jobs",
                     options.jobs,
                     "Maximum concurrent compilation jobs")
        ->check(CLI::PositiveNumber);
  }

  watch
      ->add_option("-j,--jobs",
                   options.jobs,
                   "Maximum concurrent compilation jobs")
      ->check(CLI::PositiveNumber);

  for (auto *const command :
       {build, execute, test, debug, archive, copy_runtime, foreign_runtime})
    command
        ->add_option("-t,--target",
                     target_names,
                     "Select a configured target by exact name (repeatable)")
        ->expected(1)
        ->allow_extra_args(false)
        ->take_all();

  execute->add_option("args",
                      runtime_args,
                      "Program arguments (use -- before flags)");
  debug->add_option("args",
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
         options.check_updates || options.discover_targets ||
         !options.generator.empty() || !options.cc.empty() ||
         !options.cxx.empty() || !options.build_tool_executable.empty()))
      throw std::runtime_error("compiler and generator options apply to clutch "
                               "<source-directory> configuration only");

    if (has_command && !cmake_request.arguments.empty())
      throw std::runtime_error("--cmake-arg applies to configuration only");

    if (options.check_updates &&
        (options.offline || options.update_dependencies ||
         options.discover_targets))
      throw std::runtime_error(
          "--check-updates cannot be combined with --offline, "
          "--auto-update-dependencies or --targets");

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
    // Formatting does not resolve dependencies or require an existing build.
    if (*format)
      return format_project(options, developer_tool, format_check);

    if (*lint)
      return lint_project(options, developer_tool, lint_checks);

    if (*command_runner)
      return execute_project_command(options, command_name, command_arguments);

    if (*watch) {
      if (!provider.empty() && provider != "clutch")
        throw std::runtime_error(
            "watch currently requires the native clutch provider");

      if (watch_settings.run && watch_settings.test)
        throw std::runtime_error(
            "watch --run and --test are mutually exclusive");

      if (!watch_settings.run && !watch_settings.arguments.empty())
        throw std::runtime_error("watch program arguments require --run");

      const auto executable = current_executable(arguments[0]);
      if (executable.empty())
        throw std::runtime_error("cannot resolve Clutch executable path");

      return watch_project(options, watch_settings, executable);
    }

    // Complete foreign projects retain their native generator and executor.
    // Native manifests take precedence when both project formats are present.
    if ((has_command || !source_directory.empty() ||
         options.discover_targets) &&
        providers::uses_cmake(options, provider)) {
      cmake_request.operation =
          has_command ? app.get_subcommands().front()->get_name() : "";
      cmake_request.targets = target_names;
      cmake_request.runtime_arguments = runtime_args;
      cmake_request.filter = filter;
      if (!cmake_request.prefix.empty())
        cmake_request.prefix =
            fs::absolute(fs::u8path(cmake_request.prefix)).u8string();

      return providers::run_cmake(options, cmake_request);
    }

    if (*install || *package || !cmake_request.preset.empty() ||
        !cmake_request.configuration.empty() ||
        !cmake_request.arguments.empty())
      throw std::runtime_error(
          "CMake workflow options require a CMake project or --provider cmake");
    if (*locate) {
      std::cout << file_api::locate_json(options) << '\n';
      return 0;
    }

    if (*clean) {
      clean_project(options);
      return 0;
    }

    if (*outdated) {
      report_dependency_updates(options);
      return 0;
    }

    if (*bundle) {
      const auto output =
          bundle_project(options, fs::u8path(bundle_output), bundle_exclusions);
      status("Bundled", output.u8string());
      return 0;
    }

    // A source argument selects configuration; no arguments prints usage.
    if (!has_command) {
      if (source_directory.empty() && !options.discover_targets) {
        std::cout << app.help();
        return 0;
      }

      if (options.discover_targets) {
        list_available_targets(options);
        return 0;
      }

      if (const auto executable = current_executable(arguments[0]);
          !executable.empty()) {
        const auto graph = configure(options, executable);
        status("Configured",
               graph.project.name + " [" +
                   (options.release ? "release" : "debug") + "] -> " +
                   graph.project.build.u8string());
        if (options.check_updates)
          report_dependency_updates(options);

        return 0;
      }

      throw std::runtime_error("cannot resolve Clutch executable path");
    }

    // Execution restores the validated graph and selects only requested
    // targets.
    const auto start = std::chrono::steady_clock::now();
    const auto graph = load_configuration(options);
    if (*configured_files) {
      refresh_configured_files(graph.project);
      return 0;
    }
    if (*foreign)
      return dependencies::prepare_foreign(graph, options, foreign_id);
    if (*targets) {
      for (const auto &target : graph.targets)
        std::cout << target.name << "  " << target_type_name(target.type)
                  << (target.is_test            ? " (test)"
                      : target.build_by_default ? " (default)"
                                                : "")
                  << '\n';
      return 0;
    }
    const auto selected =
        select_targets(graph,
                       target_names,
                       static_cast<bool>(*test),
                       static_cast<bool>(*execute) || static_cast<bool>(*debug),
                       filter);
    if (*foreign_runtime) {
      dependencies::stage_foreign_runtime(graph, selected);
      return 0;
    }
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
    if (*debug && !graph.project.debug_symbols)
      throw std::runtime_error("debug requires profile.debug_symbols = true; "
                               "reconfigure the selected profile");

    if (options.clean_build) {
      status("Cleaning", "selected build artifacts");
      if (const int result = clean_backend_artifacts(graph, selected))
        return result;
    }

    run_command_hooks(graph.project, options, "before-build");
    validate_inputs(graph.project.inputs);

    if (const int result =
            dependencies::build_foreign(graph, options, selected))
      return result;

    if (const int result = run(backend_build_command(graph, options, selected),
                               graph.project.build))
      return result;

    dependencies::stage_foreign_runtime(graph, selected);
    run_command_hooks(graph.project, options, "after-build");

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
    if (*debug)
      return debug_target(graph,
                          selected.front(),
                          developer_tool,
                          runtime_args);

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
  } catch (const CommandFailure &error) {
    std::cerr << "error: " << error.what() << '\n';
    return error.status();
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
}  // namespace clutch
