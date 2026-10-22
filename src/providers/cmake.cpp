/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Delegate original CMake project workflows and consume evaluated facts.
 */
#include "cmake.hpp"
#include "cmake_model.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace clutch::providers {
namespace {
// Bump only when the persisted provider routing contract changes.
constexpr auto provider_schema = 1;
// Receipt contains one UTF-8 filesystem path, not arbitrary tool output.
constexpr auto maximum_metadata_path_bytes = 32768U;

/// Fail rather than let competing workflows mutate the same source routing.
class Operation {
  public:
  explicit Operation(fs::path path)
      : path_(std::move(path)) {
    validate_generated_path(path_);
    fs::create_directories(path_.parent_path());
    if (!fs::create_directory(path_))
      throw std::runtime_error("CMake project operation is already active; "
                               "inspect " +
                               path_.u8string() + " if interrupted");
  }

  ~Operation() {
    std::error_code error;
    fs::remove(path_, error);
  }

  Operation(const Operation &) = delete;
  Operation &operator=(const Operation &) = delete;

  private:
  fs::path path_;
};

/// CMake bracket arguments preserve arbitrary paths without variable expansion.
std::string literal(const std::string &text) {
  std::string equals;
  while (text.find(']' + equals + ']') != std::string::npos)
    equals += '=';

  return '[' + equals + '[' + text + ']' + equals + ']';
}

/// Runtime selection is based on exactly one evaluated executable artifact.
const CMakeTarget &executable_target(const CMakeModel &model,
                                     const std::vector<std::string> &names) {
  const CMakeTarget *selected = nullptr;
  for (const auto &target : model.targets)
    if (target.type == "EXECUTABLE" &&
        (names.empty() || target.name == names.front())) {
      if (selected || names.size() > 1)
        throw std::runtime_error("run requires one executable --target");

      selected = &target;
    }

  if (!selected || selected->executable.empty())
    throw std::runtime_error("run requires one CMake executable with one "
                             "reported artifact; select --target <name>");

  return *selected;
}

/// Reject unknown targets before a clean-first build can remove any output.
void validate_targets(const CMakeModel &model,
                      const std::vector<std::string> &names) {
  for (const auto &name : names)
    if (std::none_of(model.targets.begin(),
                     model.targets.end(),
                     [&](const auto &target) {
                       return target.name == name;
                     }))
      throw std::runtime_error("unknown CMake target: " + name);
}

/// Preserve CMake's generator, dependency ordering and native job scheduling.
int build(const CMakeModel &model,
          const Options &options,
          const std::vector<std::string> &targets) {
  std::vector command{model.cmake,
                      std::string("--build"),
                      model.build.u8string()};
  if (!model.configuration.empty())
    command.insert(command.end(), {"--config", model.configuration});

  if (options.jobs)
    command.insert(command.end(), {"--parallel", std::to_string(options.jobs)});

  if (options.clean_build)
    command.emplace_back("--clean-first");

  if (!targets.empty()) {
    command.emplace_back("--target");
    command.insert(command.end(), targets.begin(), targets.end());
  }

  return run(command, model.source);
}

/// Treat --filter as a literal substring, matching the native Clutch interface.
std::string test_pattern(const std::string &text) {
  std::string pattern;
  for (const auto character : text) {
    if (std::string_view(R"(\.^$|()[]{}*+?)").find(character) !=
        std::string_view::npos)
      pattern += '\\';

    pattern += character;
  }

  return pattern;
}

/// Presets for execution are explicit independent native workflows. CMake,
/// CTest and CPack evaluate their own inheritance, conditions and environment.
int execute_preset(const Options &options, const CMakeRequest &request) {
  if (request.operation != "build" && request.operation != "test" &&
      request.operation != "package")
    throw std::runtime_error(
        "execution presets apply to build, test or package; "
        "run uses the saved configuration's artifact");

  if (options.release || !request.configuration.empty() ||
      !request.targets.empty() || !request.filter.empty() || options.jobs ||
      options.clean_build)
    throw std::runtime_error("execution presets own configuration, targets, "
                             "filters and jobs; set these in the preset");

  const auto program = find_program(request.operation == "build"  ? "cmake"
                                    : request.operation == "test" ? "ctest"
                                                                  : "cpack");
  if (program.empty())
    throw std::runtime_error("CMake workflow executable is unavailable");

  std::vector command{program};
  if (request.operation == "build")
    command.emplace_back("--build");

  command.insert(command.end(), {"--preset", request.preset});
  return run(command, options.root);
}
}  // namespace

bool uses_cmake(const Options &options, const std::string &provider) {
  if (!provider.empty() && provider != "clutch" && provider != "cmake")
    throw std::runtime_error("project provider must be clutch or cmake");

  return provider == "cmake" ||
         (provider.empty() && !fs::exists(options.root / "clutch.toml") &&
          fs::is_regular_file(options.root / "CMakeLists.txt"));
}

int run_cmake(const Options &options, const CMakeRequest &request) {
  const auto source = fs::canonical(options.root);
  if (!fs::is_regular_file(source / "CMakeLists.txt"))
    throw std::runtime_error(
        "CMake provider requires CMakeLists.txt at the source root");

  if (options.locked || options.offline || options.update_dependencies ||
      options.check_updates || !options.cc.empty() || !options.cxx.empty() ||
      !options.build_tool_executable.empty())
    throw std::runtime_error(
        "native dependency/compiler options do not apply to "
        "CMake projects; use CMake presets or --cmake-arg");

  const std::set<std::string> operations{"",
                                         "build",
                                         "run",
                                         "test",
                                         "clean",
                                         "targets",
                                         "locate",
                                         "install",
                                         "package"};
  if (!operations.count(request.operation))
    throw std::runtime_error("operation is unavailable for CMake projects: " +
                             request.operation);

  for (const auto &argument : request.arguments)
    if (argument.rfind("-D", 0) != 0 && argument.rfind("-U", 0) != 0 &&
        argument.rfind("-W", 0) != 0 && argument.rfind("-A", 0) != 0 &&
        argument.rfind("-T", 0) != 0 && argument.rfind("--toolchain=", 0) != 0)
      throw std::runtime_error(
          "--cmake-arg accepts inline -D/-U/-W/-A/-T or "
          "--toolchain= settings; source/build routing is provider-owned");

  const auto routing = source / ".clutch/cmake";
  const Operation operation(routing / "operation.lock");
  const auto *const profile = options.release ? "release" : "debug";
  const auto state_path = routing / (std::string(profile) + ".json");
  const auto pending = routing / (std::string(profile) + ".pending");
  if (!request.preset.empty() && !request.operation.empty())
    return execute_preset(options, request);

  if (request.operation.empty()) {
    const auto cmake = find_program("cmake");
    if (cmake.empty())
      throw std::runtime_error("CMake project provider requires CMake 3.21+");

    // A preload runs in CMake's resolved binary directory, including preset
    // inheritance and macros. It adds only our private queries and a receipt;
    // the user's project remains the original top-level CMake project.
    const auto receipt = routing / "binary-directory.txt";
    const auto preload = routing / "query.cmake";
    write_if_changed(
        preload,
        "# Generated by Clutch: request facts without changing project "
        "policy.\n"
        "file(MAKE_DIRECTORY "
        "\"${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-clutch-project\")\n"
        "foreach(kind IN ITEMS codemodel-v2 cache-v2 cmakeFiles-v1 "
        "toolchains-v1)\n"
        "  file(WRITE "
        "\"${CMAKE_BINARY_DIR}/.cmake/api/v1/query/client-clutch-project/"
        "${kind}\" \"\")\n"
        "endforeach()\nfile(WRITE " +
            literal(receipt.generic_u8string()) +
            " \"${CMAKE_BINARY_DIR}\")\n");
    validate_generated_path(receipt);
    fs::remove(receipt);
    write_if_changed(pending,
                     "Configuration has not completed successfully.\n");
    std::vector command{cmake};
    if (!request.preset.empty())
      command.insert(command.end(), {"--preset", request.preset});
    else {
      const auto binary = source / (std::string("Clutch-build-") + profile);
      validate_generated_path(binary);
      command.insert(
          command.end(),
          {"-S",
           source.u8string(),
           "-B",
           binary.u8string(),
           "-DCMAKE_BUILD_TYPE=" +
               (request.configuration.empty()
                    ? std::string(options.release ? "Release" : "Debug")
                    : request.configuration),
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"});
    }

    if (!options.generator.empty())
      command.insert(command.end(), {"-G", options.generator});

    // Keep the receipt/query destination authoritative even if a caller's
    // additional arguments include their own preload script.
    command.insert(command.end(),
                   request.arguments.begin(),
                   request.arguments.end());
    command.insert(command.end(), {"-C", preload.u8string()});
    if (const int result = run(command, source))
      return result;

    validate_generated_path(receipt);
    if (!fs::is_regular_file(receipt) ||
        fs::file_size(receipt) > maximum_metadata_path_bytes)
      throw std::runtime_error("CMake did not report its binary directory");

    std::ifstream input(receipt, std::ios::binary);
    const std::string path((std::istreambuf_iterator<char>(input)), {});
    auto configuration = request.configuration;
    if (configuration.empty() && options.release)
      configuration = "Release";

    const auto model =
        read_cmake_model(source, fs::u8path(path), configuration);
    const nlohmann::json state{{"schema", provider_schema},
                               {"provider", "cmake"},
                               {"source", source.generic_u8string()},
                               {"build", model.build.generic_u8string()},
                               {"configuration", model.configuration}};
    auto identity = state;
    identity.erase("configuration");
    write_if_changed(model.build / ".clutch/provider.json",
                     identity.dump(2) + '\n');
    write_if_changed(state_path, state.dump(2) + '\n');
    fs::remove(pending);
    status("Configured", "CMake project -> " + model.build.u8string());
    if (options.discover_targets)
      for (const auto &target : model.targets)
        std::cout << target.name << "  " << target.type << '\n';

    return 0;
  }

  if (fs::exists(pending))
    throw std::runtime_error(
        "the last CMake configuration did not complete; configure again");

  if (!fs::exists(state_path))
    throw std::runtime_error(
        "CMake project is not configured; run clutch <source-directory>");

  const auto state = read_cmake_json(state_path);
  if (state.at("schema") != provider_schema ||
      state.at("provider") != "cmake" ||
      fs::canonical(fs::u8path(state.at("source").get<std::string>())) !=
          source)
    throw std::runtime_error(
        "invalid saved CMake project identity; configure again");

  const auto binary = fs::u8path(state.at("build").get<std::string>());
  auto identity = state;
  identity.erase("configuration");
  if (!binary.is_absolute() || !fs::is_directory(binary) ||
      read_cmake_json(binary / ".clutch/provider.json") != identity)
    throw std::runtime_error(
        "saved CMake build identity changed; configure again");

  const Operation build_operation(binary / ".clutch/project-operation.lock");
  auto model =
      read_cmake_model(source,
                       binary,
                       request.configuration.empty()
                           ? state.at("configuration").get<std::string>()
                           : request.configuration);
  if (request.operation == "locate") {
    // Other generators can leave an old database behind when a build tree is
    // repurposed. Its presence alone does not make it a current IDE blueprint.
    const bool exports_commands =
        model.generator.rfind("Ninja", 0) == 0 ||
        model.generator.find("Makefiles") != std::string::npos;
    std::cout
        << nlohmann::json{{"provider", "cmake"},
                          {"source", source.generic_u8string()},
                          {"build", model.build.generic_u8string()},
                          {"configuration", model.configuration},
                          {"generator", model.generator},
                          {"fileApiIndex", model.index.generic_u8string()},
                          {"compilationDatabase",
                           exports_commands &&
                                   fs::is_regular_file(model.build /
                                                       "compile_commands.json")
                               ? nlohmann::json(
                                     (model.build / "compile_commands.json")
                                         .generic_u8string())
                               : nlohmann::json(nullptr)}}
               .dump(2)
        << '\n';
    return 0;
  }

  if (request.operation == "targets") {
    for (const auto &target : model.targets)
      std::cout << target.name << "  " << target.type << '\n';

    return 0;
  }

  validate_targets(model, request.targets);
  auto selected = request.targets;
  if (request.operation == "run")
    selected = {executable_target(model, selected).name};

  if (request.operation == "clean")
    return build(model, options, {"clean"});

  if (request.operation == "package" &&
      !fs::is_regular_file(binary / "CPackConfig.cmake"))
    throw std::runtime_error(
        "this CMake project does not define CPack packaging");

  if (const int result = build(model, options, selected))
    return result;

  // Native builds may regenerate CMake after source/configuration edits. Never
  // launch an artifact retained from the earlier snapshot after regeneration.
  model = read_cmake_model(source, binary, model.configuration);
  if (request.operation == "build")
    return 0;

  if (request.operation == "run") {
    const auto &target = executable_target(model, selected);
    const auto &artifact = target.executable;
    if (!fs::is_regular_file(artifact))
      throw std::runtime_error("CMake executable artifact is missing: " +
                               artifact.u8string());

    std::vector command{artifact.u8string()};
    command.insert(command.end(),
                   request.runtime_arguments.begin(),
                   request.runtime_arguments.end());
    return run(command, source);
  }

  if (request.operation == "test") {
    std::vector command{model.ctest,
                        std::string("--output-on-failure"),
                        std::string("--no-tests=error")};
    if (!model.configuration.empty())
      command.insert(command.end(), {"-C", model.configuration});

    if (options.jobs)
      command.insert(command.end(),
                     {"--parallel", std::to_string(options.jobs)});

    if (!request.filter.empty())
      command.insert(command.end(), {"-R", test_pattern(request.filter)});

    return run(command, binary);
  }

  if (request.operation == "install") {
    std::vector command{model.cmake,
                        std::string("--install"),
                        binary.u8string()};
    if (!model.configuration.empty())
      command.insert(command.end(), {"--config", model.configuration});

    if (!request.prefix.empty())
      command.insert(command.end(), {"--prefix", request.prefix});

    if (!request.component.empty())
      command.insert(command.end(), {"--component", request.component});

    return run(command, source);
  }

  if (request.operation == "package") {
    std::vector command{model.cpack,
                        std::string("--config"),
                        (binary / "CPackConfig.cmake").u8string()};
    if (!model.configuration.empty())
      command.insert(command.end(), {"-C", model.configuration});

    return run(command, binary);
  }

  throw std::runtime_error("operation is unavailable for CMake projects: " +
                           request.operation);
}
}  // namespace clutch::providers
