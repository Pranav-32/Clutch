/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Apply evaluated package interfaces and schedule upstream native
 * builds.
 */
#include "foreign.hpp"
#include "cmake.hpp"
#include "core/build_graph.hpp"
#include "core/options.hpp"
#include "core/pch/headers.hpp"
#include "core/project_layout.hpp"
#include "core/targets/requirements.hpp"
#include "make.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include "system.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace clutch::dependencies {
namespace {
/// Serialize access to native dependency output trees within one profile.
class Writer {
  public:
  explicit Writer(fs::path path, const bool wait = false)
      : path_(std::move(path)) {
    validate_generated_path(path_);
    fs::create_directories(path_.parent_path());
    // Native executors can schedule independent preparation nodes together.
    // Serialize their upstream job pools; ordinary competing CLI operations
    // still fail immediately. A stale lock never causes an unbounded wait.
    constexpr auto lock_timeout = std::chrono::minutes(5);
    constexpr auto poll_interval = std::chrono::milliseconds(100);
    const auto deadline = std::chrono::steady_clock::now() + lock_timeout;
    while (!fs::create_directory(path_)) {
      if (!wait || std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error(
            "native dependency operation is already active; inspect " +
            path_.u8string() + " if interrupted");

      std::this_thread::sleep_for(poll_interval);
      validate_generated_path(path_);
    }
  }

  ~Writer() {
    std::error_code error;
    fs::remove(path_, error);
  }

  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;

  private:
  fs::path path_;
};

/// Keep incoming interfaces stable while preserving meaningful link order.
template <class T>
void append(std::vector<T> &destination, const std::vector<T> &source) {
  for (const auto &entry : source)
    if (std::find(destination.begin(), destination.end(), entry) ==
        destination.end())
      destination.push_back(entry);
}

/// An identity must have one upstream configuration shared by its consumers.
bool compatible(const Requirement &a, const Requirement &b) {
  return a.build_system == b.build_system && a.root == b.root &&
         a.link_language == b.link_language &&
         a.cmake_options == b.cmake_options &&
         a.build_arguments == b.build_arguments &&
         (a.provider == "system" || a.libraries == b.libraries) &&
         a.baseline == b.baseline;
}

/// Follow native target dependencies before choosing upstream actions.
std::set<std::string> required_builds(const Graph &graph,
                                      const std::vector<size_t> &selected) {
  std::set<std::string> required;
  std::set<size_t> visited;
  auto pending = selected;
  while (!pending.empty()) {
    const auto index = pending.back();
    pending.pop_back();
    if (!visited.insert(index).second)
      continue;
    const auto &target = graph.targets.at(index);
    required.insert(target.foreign_dependencies.begin(),
                    target.foreign_dependencies.end());
    pending.insert(pending.end(),
                   target.dependencies.begin(),
                   target.dependencies.end());
  }
  return required;
}
}  // namespace

void configure_foreign(Project &project,
                       const Toolchain &tools,
                       const BuildBackend &backend,
                       const Options &options) {
  if (project.foreign_requests.empty())
    return;
  const Writer writer(project.build / layout::environment / "foreign.lock");
  std::map<std::string, Requirement> settings;
  std::map<std::string, NativeInterface> inspected;
  for (const auto &request : project.foreign_requests) {
    const auto &r = request.requirement;
    if ((r.build_system != "make" && r.provider != "system" &&
         !r.libraries.empty()) ||
        (r.build_system != "make" && !r.build_arguments.empty()))
      throw std::runtime_error(
          "dependency libraries require Make or explicit system metadata; "
          "build_arguments belong to the Make adapter");
    if (r.build_system == "make" && !r.cmake_options.empty())
      throw std::runtime_error(
          "dependency.cmake_options requires the CMake adapter");
    if (const auto previous = settings.find(request.identity);
        previous != settings.end()) {
      if (!compatible(previous->second, r))
        throw std::runtime_error(
            "conflicting native build settings for dependency " + r.alias);
    } else
      settings.emplace(request.identity, r);
    std::string selection = request.identity + ':' + r.target + ':' +
                            r.version + ':' + r.cmake_package;
    for (const auto &component : r.components)
      selection += ':' + component;
    for (const auto &target : r.targets)
      selection += ':' + target;
    for (const auto &define : r.defines)
      selection += ':' + std::to_string(define.size()) + ':' + define;
    for (const auto &library : r.libraries)
      selection += ':' + std::to_string(library.size()) + ':' + library;
    if (!inspected.count(selection)) {
      const bool installed =
          r.provider == "system" && r.target.empty() && r.targets.empty() &&
          (!r.libraries.empty() ||
           (!r.include_dirs.empty() && r.cmake_package.empty() &&
            r.components.empty() && r.version.empty() &&
            r.cmake_options.empty()));
      status("Configuring",
             r.alias + " (" + (installed ? "system" : r.build_system) + ")");
      const bool make = r.build_system == "make" ||
                        (r.build_system == "auto" && !request.source.empty() &&
                         !fs::exists(request.source / "CMakeLists.txt") &&
                         fs::exists(request.source / "Makefile"));
      if (!r.link_language.empty() && !installed && !make)
        throw std::runtime_error(
            "dependency.link_language requires Make or an explicit system "
            "interface; "
            "CMake supplies its own link-language metadata");
      auto result =
          installed ? inspect_system(request, tools)
          : make    ? inspect_make(request, project, tools)
                    : inspect_cmake(request, project, tools, backend, options);
      if (options.discover_targets) {
        std::cout << "Dependency " << r.alias << ":\n";
        for (const auto &[name, type] : result.available_targets)
          std::cout << "  " << name << "  " << type << '\n';

        if (result.available_targets.empty())
          std::cout << "  (no discoverable native library targets)\n";
        inspected.emplace(selection, std::move(result));
        continue;
      }

      for (const auto &input : result.inputs)
        if (std::none_of(project.inputs.begin(),
                         project.inputs.end(),
                         [&](const ConfigurationInput &entry) {
                           return entry.path == input.path;
                         }))
          project.inputs.push_back(input);
      if (!result.build.arguments.empty())
        project.foreign_builds.push_back(result.build);
      if (!result.version.empty())
        for (auto &package : project.packages)
          if (identity(package) == request.identity) {
            if (package.version != "unversioned" &&
                package.version != result.version)
              throw std::runtime_error("conflicting installed versions for " +
                                       r.alias);
            package.version = result.version;
          }
      inspected.emplace(selection, std::move(result));
    }
    if (options.discover_targets)
      continue;

    auto usage = inspected.at(selection).usage;
    usage.defines = merge_defines(usage.defines, r.defines);
    const auto source_root = !request.source.empty() ? request.source
                             : r.root.empty()        ? project.root
                                                     : fs::u8path(r.root);
    if (r.build_system != "make") {
      for (const auto &directory : r.include_dirs) {
        const auto path = fs::canonical(source_root / fs::u8path(directory));
        if (!fs::is_directory(path))
          throw std::runtime_error(
              "dependency include_dirs must name directories");
        append(usage.includes, std::vector{path});
      }
      for (const auto &runtime : r.runtime)
        append(usage.runtime_libraries,
               std::vector{fs::canonical(source_root / fs::u8path(runtime))});
    }
    const auto cpp_headers = pch::paths(r.pch, source_root);
    const auto c_headers = pch::paths(r.pch_c, source_root);
    for (auto &build : project.foreign_builds)
      if (build.id == usage.id)
        append(build.interface_dirs, usage.includes);
    // Header-only packages have no native target. Carry their foreign usage
    // through incoming package edges until a real consuming project is found.
    std::set<std::string> owners, visited;
    std::function<void(const std::string &)> consumers =
        [&](const std::string &owner) {
          if (!visited.insert(owner).second)
            return;
          for (const auto *const group : {&project.targets, &project.tests})
            if (std::any_of(group->begin(),
                            group->end(),
                            [&](const TargetSpec &target) {
                              return (target.project_name.empty()
                                          ? project.name
                                          : target.project_name) == owner;
                            })) {
              owners.insert(owner);
              return;
            }
          for (const auto &[consumer, dependency] : project.package_edges)
            if (dependency == owner)
              consumers(consumer);
          for (const auto &package : project.packages)
            if (std::find(package.dependencies.begin(),
                          package.dependencies.end(),
                          owner) != package.dependencies.end())
              consumers(package.id);
        };
    consumers(request.owner);
    for (auto *const group : {&project.targets, &project.tests})
      for (auto &target : *group) {
        const auto owner =
            target.project_name.empty() ? project.name : target.project_name;
        if (!owners.count(owner))
          continue;
        // Alias links have already chosen their upstream target in the import.
        const auto alias = project.is_workspace && !target.external
                               ? owner + "::" + r.alias
                               : r.alias;
        for (auto *const links : {&target.links, &target.public_links})
          links->erase(
              std::remove_if(links->begin(),
                             links->end(),
                             [&](const std::string &link) {
                               if (link == alias)
                                 return true;
                               if (link.rfind(alias + "::", 0) != 0)
                                 return false;
                               const auto name = link.substr(alias.size() + 2);
                               if (name != r.target &&
                                   std::find(r.targets.begin(),
                                             r.targets.end(),
                                             name) == r.targets.end())
                                 throw std::runtime_error(
                                     "foreign library link must select a "
                                     "target declared by dependency '" +
                                     r.alias + "'");
                               return true;
                             }),
              links->end());
        if (const auto found = std::find_if(target.dependency_usage.begin(),
                                            target.dependency_usage.end(),
                                            [&](const DependencyUsage &item) {
                                              return item.id == usage.id;
                                            });
            found == target.dependency_usage.end())
          target.dependency_usage.push_back(usage);
        else {
          found->defines = merge_defines(found->defines, usage.defines);
          append(found->includes, usage.includes);
          append(found->runtime_libraries, usage.runtime_libraries);
        }
        append(target.pch_cpp, cpp_headers);
        append(target.pch_c, c_headers);
      }
    project.package_edges.emplace_back(request.owner,
                                       source_key(request.identity));
  }
}

namespace {
/// One execution path serves CLI builds and native-graph preparation nodes.
int execute_foreign(const Graph &graph,
                    const Options &options,
                    const std::set<std::string> &required) {
  if (graph.project.foreign_builds.empty() || required.empty())
    return 0;
  const auto processors = std::max(1U, std::thread::hardware_concurrency());
  const auto jobs =
      options.jobs > 0
          ? options.jobs
          : static_cast<int>(std::min(
                processors,
                static_cast<unsigned>(std::numeric_limits<int>::max())));
  for (const auto &build : graph.project.foreign_builds) {
    if (!required.count(build.id))
      continue;
    auto command =
        options.clean_build ? build.rebuild_arguments : build.arguments;
    if (command.empty())
      throw std::runtime_error(
          "native dependency has no clean-build action; configure again");
    for (auto &argument : command)
      if (argument == "@CLUTCH_JOBS@")
        argument = std::to_string(jobs);
    status("Building", build.id + " (native dependency)");
    if (const int code = run(command, build.directory))
      return code;
    for (const auto &artifact : build.artifacts)
      if (!fs::is_regular_file(artifact))
        throw std::runtime_error("native dependency did not produce " +
                                 artifact.u8string());
  }
  return 0;
}
}  // namespace

int build_foreign(const Graph &graph,
                  const Options &options,
                  const std::vector<size_t> &selected) {
  const auto required = required_builds(graph, selected);
  if (required.empty())
    return 0;

  const Writer writer(graph.project.build / layout::environment /
                      "foreign.lock");
  return execute_foreign(graph, options, required);
}

fs::path foreign_stamp(const Graph &graph, const std::string &id) {
  return graph.project.build / layout::environment / "foreign" /
         (id + ".stamp");
}

int prepare_foreign(const Graph &graph,
                    const Options &options,
                    const std::string &id) {
  const auto build = std::find_if(graph.project.foreign_builds.begin(),
                                  graph.project.foreign_builds.end(),
                                  [&](const ForeignBuild &action) {
                                    return action.id == id;
                                  });
  if (build == graph.project.foreign_builds.end())
    throw std::runtime_error("unknown native dependency action: " + id);

  const Writer writer(graph.project.build / layout::environment /
                          "foreign.lock",
                      true);
  if (const int result = execute_foreign(graph, options, {id}))
    return result;

  // Include membership and native timestamps, not archive contents: scanning
  // huge binaries on every incremental build defeats the native executor's
  // cache. Explicit --clean remains the repair path for unchanged timestamps.
  constexpr size_t maximum_interface_entries = 100000;
  std::set<fs::path> files(build->artifacts.begin(), build->artifacts.end());
  size_t entries = 0;
  for (const auto &directory : build->interface_dirs) {
    if (!fs::is_directory(directory))
      continue;

    for (fs::recursive_directory_iterator current(directory), end;
         current != end;
         ++current) {
      if (++entries > maximum_interface_entries)
        throw std::runtime_error(
            "native dependency interface exceeds scan limit");

      if (current->is_symlink()) {
        current.disable_recursion_pending();
        continue;
      }

      if (current->is_directory()) {
        if (const auto name = current->path().filename();
            name == ".git" || name == layout::environment)
          current.disable_recursion_pending();
      } else if (current->is_regular_file())
        files.insert(current->path());
    }
  }
  std::ostringstream state;
  for (const auto &file : files)
    state << file.generic_u8string().size() << ':' << file.generic_u8string()
          << ':' << fs::file_size(file) << ':'
          << file_time_text(fs::last_write_time(file)) << '\n';

  write_if_changed(foreign_stamp(graph, id), state.str());
  return 0;
}

void stage_foreign_runtime(const Graph &graph,
                           const std::vector<size_t> &selected) {
#ifdef _WIN32
  // Executables can share a destination directory while native build nodes
  // stage DLLs concurrently. Compare/copy under one profile-local writer.
  const Writer writer(graph.project.build / layout::environment /
                          "foreign-runtime.lock",
                      true);
  for (const auto index : selected) {
    const auto &target = graph.targets.at(index);
    if (target.type != TargetType::Executable)
      continue;
    std::map<fs::path, fs::path> destinations;
    std::set<size_t> visited;
    std::vector<size_t> pending{index};
    while (!pending.empty()) {
      const auto provider = pending.back();
      pending.pop_back();
      if (!visited.insert(provider).second)
        continue;
      const auto &dependency = graph.targets.at(provider);
      pending.insert(pending.end(),
                     dependency.dependencies.begin(),
                     dependency.dependencies.end());
      for (const auto &library : dependency.runtime_libraries) {
        const auto destination =
            target.output.parent_path() / library.filename();
        validate_generated_path(destination);
        if (const auto found = destinations.find(destination);
            found != destinations.end() && found->second != library)
          throw std::runtime_error("runtime dependency filename collision: " +
                                   destination.u8string());
        destinations[destination] = library;
        if (library != destination &&
            (!fs::is_regular_file(destination) ||
             input_signature(library) != input_signature(destination)))
          fs::copy_file(library,
                        destination,
                        fs::copy_options::overwrite_existing);
      }
    }
  }
#else
  (void)graph;
  (void)selected;
#endif
}
}  // namespace clutch::dependencies
