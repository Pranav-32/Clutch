/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Backtrack version choices with cached metadata and deterministic
 * edges.
 */

#include "resolve.hpp"
#include "core/options.hpp"
#include "core/pch/headers.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "core/targets/requirements.hpp"
#include "core/workspace/workspace.hpp"
#include "manifest.hpp"
#include "platform/terminal.hpp"
#include "sources.hpp"
#include "version.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <toml.hpp>
#include <utility>

namespace clutch::dependencies {
namespace {
// Bound adversarial metadata and exponential searches with actionable failures.
constexpr size_t maximum_packages = 1024, maximum_decisions = 10000;

struct Metadata {
  Candidate candidate;
  std::vector<Requirement> requirements;
  bool declared_version = false;
};

/// One decision variable per Git identity prevents duplicate-version ABI
/// graphs.
class Solver {
  public:
  explicit Solver(Sources &store)
      : store_(store) {}

  /// Restart decisions while retaining metadata fetched for previous attempts.
  std::map<std::string, Metadata> run(const std::vector<Request> &roots) {
    requests_.clear();
    selected_.clear();
    decisions_ = 0;
    for (const auto &request : roots)
      requests_[identity(request.requirement)].push_back(request);
    if (search())
      return selected_;

    return {};
  }

  /// Preserve the last actionable conflict when every candidate fails.
  const std::string &error() const { return error_; }

  private:
  Sources &store_;
  std::map<std::string, std::vector<Request>> requests_;
  std::map<std::string, Metadata> selected_;
  std::map<std::pair<std::string, std::string>, Metadata> metadata_;
  size_t decisions_ = 0;
  std::string error_;

  /// Cache source parsing across backtracking; each commit is materialized
  /// once.
  Metadata metadata(Candidate candidate) {
    const auto key = std::make_pair(candidate.identity, candidate.revision);
    if (const auto found = metadata_.find(key); found != metadata_.end()) {
      auto result = found->second;
      result.candidate.reference = candidate.reference;
      if (!result.declared_version)
        result.candidate.version =
            candidate.version.empty() ? "unversioned" : candidate.version;
      return result;
    }

    candidate = store_.materialize(std::move(candidate));
    Metadata result{candidate, {}};
    if (const auto manifest = candidate.source / layout::manifest;
        !candidate.source.empty() && fs::exists(manifest)) {
      if (!fs::is_regular_file(manifest) || fs::is_symlink(manifest))
        throw std::runtime_error(
            "dependency clutch.toml must be a regular file");
      const auto document = toml::parse_file(manifest.u8string());
      if (document.contains("workspace"))
        throw std::runtime_error("Git imports currently require one library "
                                 "manifest, without [workspace]");
      result.requirements = read_requirements(document, candidate.source);
      // A relative self-import denotes this repository, not a new identity at
      // each cache location. Normalize it before cycle detection or fetching.
      for (auto &requirement : result.requirements) {
        if (requirement.provider == "git" &&
            fs::u8path(requirement.git) == candidate.source)
          requirement.git = candidate.identity;
        store_.normalize(requirement);
      }

      if (const auto version =
              document["project"]["version"].value<std::string>();
          version) {
        result.candidate.version = *version;
        result.declared_version = true;
      }
    }

    if (result.candidate.version.empty())
      result.candidate.version = "unversioned";
    metadata_.emplace(key, result);
    return result;
  }

  /// Retain requester identities and selectors, not only the last failed edge.
  void conflict(const std::string &git) {
    std::ostringstream message;
    message << "dependency version conflict for " << git << ":\n";
    for (const auto &request : requests_.at(git)) {
      const auto &r = request.requirement;
      auto owner = request.owner;
      for (const auto &[source, chosen] : selected_) {
        (void)chosen;
        if (source_key(source) == owner)
          owner = source;
      }

      message << "  " << owner << " -> " << r.alias << " requires ";
      if (!r.tag.empty())
        message << "tag " << r.tag << ' ';
      if (!r.branch.empty())
        message << "branch " << r.branch << ' ';
      if (!r.revision.empty())
        message << "commit " << r.revision << ' ';
      message << (r.version.empty() ? "a compatible revision" : r.version)
              << '\n';
    }

    error_ = message.str();
  }

  /// Validate package-level cycles independently from concrete native targets.
  bool acyclic() {
    enum class Visit {
      New,
      Active,
      Done
    };
    std::map<std::string, Visit> state;
    std::vector<std::string> chain;
    std::function<bool(const std::string &)> visit =
        [&](const std::string &git) {
          if (state[git] == Visit::Done)
            return true;

          if (state[git] == Visit::Active) {
            std::ostringstream message;
            message << "dependency cycle:";
            for (const auto &entry : chain)
              message << ' ' << entry << " ->";
            message << ' ' << git;
            error_ = message.str();
            return false;
          }

          state[git] = Visit::Active;
          chain.push_back(git);
          if (const auto found = selected_.find(git); found != selected_.end())
            for (const auto &dependency : found->second.requirements)
              if (!visit(identity(dependency)))
                return false;

          chain.pop_back();
          state[git] = Visit::Done;
          return true;
        };
    for (const auto &[git, metadata] : selected_) {
      (void)metadata;
      if (!visit(git))
        return false;
    }

    return true;
  }

  /// Most-constrained-first selection narrows the search before cloning
  /// sources.
  bool search() {
    if (requests_.size() > maximum_packages || ++decisions_ > maximum_decisions)
      throw std::runtime_error("dependency resolution exceeds package/search "
                               "limits; narrow version requirements");
    for (const auto &[git, chosen] : selected_)
      for (const auto &request : requests_.at(git))
        if (!store_.accepts(chosen.candidate, request)) {
          conflict(git);
          return false;
        }

    if (!acyclic())
      return false;

    std::string next;
    std::vector<Candidate> choices;
    for (const auto &[git, requests] : requests_) {
      if (selected_.count(git))
        continue;

      auto candidates = store_.candidates(git, requests);
      if (candidates.empty()) {
        conflict(git);
        return false;
      }

      if (next.empty() || candidates.size() < choices.size()) {
        next = git;
        choices = std::move(candidates);
      }
    }

    if (next.empty())
      return true;

    for (auto &candidate : choices) {
      auto choice = metadata(std::move(candidate));
      if (std::any_of(requests_.at(next).begin(),
                      requests_.at(next).end(),
                      [&](const Request &request) {
                        return !store_.accepts(choice.candidate, request);
                      })) {
        conflict(next);
        continue;
      }

      std::vector<std::string> added;
      selected_.emplace(next, choice);
      for (const auto &requirement : choice.requirements) {
        requests_[identity(requirement)].push_back(
            {source_key(next), requirement});
        added.push_back(identity(requirement));
      }

      if (search())
        return true;

      for (auto iterator = added.rbegin(); iterator != added.rend();
           ++iterator) {
        auto &requests = requests_.at(*iterator);
        requests.pop_back();
        if (requests.empty())
          requests_.erase(*iterator);
      }

      selected_.erase(next);
    }

    return false;
  }
};

/// Preserve declaration order while removing duplicate inherited usage entries.
template <class T>
void append(std::vector<T> &destination, const std::vector<T> &source) {
  for (const auto &value : source)
    if (std::find(destination.begin(), destination.end(), value) ==
        destination.end())
      destination.push_back(value);
}

/// Library paths stay tied to the chosen checkout, not the consuming project's
/// cwd.
std::vector<fs::path> includes(const Candidate &candidate,
                               const Requirement &requirement) {
  std::vector<fs::path> result;
  auto directories = requirement.include_dirs;
  if (directories.empty())
    directories.push_back(
        fs::is_directory(candidate.source / "include") ? "include" : ".");
  for (const auto &directory : directories) {
    const auto path = fs::canonical(candidate.source / fs::u8path(directory));
    if (!fs::is_directory(path))
      throw std::runtime_error(
          "dependency include_dirs contains a non-directory");
    result.push_back(path);
  }

  return result;
}

/// Resolve explicit aliases before project-local target qualification is lost.
void apply(Project &project,
           const std::string &owner,
           const std::vector<Requirement> &requirements,
           const std::map<std::string, Metadata> &packages,
           const std::map<std::string, std::vector<std::string>> &exports,
           const bool direct = true,
           std::set<std::string> *expanded = nullptr) {
  std::set<std::string> local_expansion;
  if (!expanded)
    expanded = &local_expansion;
  for (const auto &requirement : requirements) {
    const auto &candidate = packages.at(identity(requirement)).candidate;
    if (std::any_of(project.foreign_requests.begin(),
                    project.foreign_requests.end(),
                    [&](const ForeignRequest &request) {
                      return request.identity == candidate.identity;
                    }))
      continue;
    const auto id = source_key(candidate.identity);
    std::string library;
    if (const auto found = exports.find(candidate.identity);
        found != exports.end()) {
      const auto &names = found->second;
      if (!requirement.target.empty()) {
        library = id + "::" + requirement.target;
        if (std::find(names.begin(), names.end(), library) == names.end())
          throw std::runtime_error(
              "dependency target is not an exported library: " +
              requirement.alias + "::" + requirement.target);
      } else if (names.size() == 1)
        library = names.front();
      else
        throw std::runtime_error("dependency '" + requirement.alias +
                                 "' exports multiple libraries; select target");
    } else if (!requirement.target.empty())
      throw std::runtime_error(
          "header-only dependency has no compiled target: " +
          requirement.alias);
    else if (expanded->insert(candidate.identity).second)
      // Header-only packages have no target to carry their transitive
      // interface.
      apply(project,
            owner,
            packages.at(identity(requirement)).requirements,
            packages,
            exports,
            false,
            expanded);
    const auto directories =
        library.empty() || !requirement.include_dirs.empty()
            ? includes(candidate, requirement)
            : std::vector<fs::path>{};
    const auto cpp_headers = pch::paths(requirement.pch, candidate.source);
    const auto c_headers = pch::paths(requirement.pch_c, candidate.source);
    for (auto *const group : {&project.targets, &project.tests})
      for (auto &target : *group) {
        const auto target_owner =
            target.project_name.empty() ? project.name : target.project_name;
        if (target_owner != owner)
          continue;

        const auto local_alias = project.is_workspace && !target.external
                                     ? owner + "::" + requirement.alias
                                     : requirement.alias;
        if (direct)
          for (auto *const links : {&target.links, &target.public_links}) {
            for (auto &link : *links)
              if (link.rfind(local_alias + "::", 0) == 0) {
                const auto qualified = id + link.substr(local_alias.size());
                const auto found = exports.find(candidate.identity);
                if (found == exports.end() ||
                    std::find(found->second.begin(),
                              found->second.end(),
                              qualified) == found->second.end())
                  throw std::runtime_error("unknown dependency library: " +
                                           link);
                link = qualified;
              }

            links->erase(std::remove(links->begin(), links->end(), local_alias),
                         links->end());
          }

        if (!library.empty())
          append(target.public_links, std::vector{library});
        append(target.public_includes, directories);
        target.public_defines =
            merge_defines(target.public_defines, requirement.defines);
        append(target.pch_cpp, cpp_headers);
        append(target.pch_c, c_headers);
      }

    if (direct)
      project.package_edges.emplace_back(owner, id);
  }
}
}  // namespace

void resolve(Project &project, const Options &options) {
  if (project.dependency_requests.empty())
    return;
  status("Resolving", "dependency graph");
  Sources store(project.root, options);
  for (auto &request : project.dependency_requests)
    store.normalize(request.requirement);
  if (options.update_dependencies)
    store.explore_alternatives(false);
  Solver solver(store);
  auto packages = solver.run(project.dependency_requests);
  if (packages.empty() && !options.locked && !options.offline &&
      !options.update_dependencies) {
    store.explore_alternatives();
    packages = solver.run(project.dependency_requests);
  }

  if (packages.empty())
    throw std::runtime_error(
        solver.error() + (options.locked
                              ? "clutch.lock is missing or outdated (--locked)"
                              : ""));

  // Preserve the standalone application's public CLI identity when imports add
  // additional library targets to its internal graph.
  if (project.targets.empty()) {
    TargetSpec application;
    application.name = project.name;
    application.sources = project.sources;
    application.additional_files = project.additional_files;
    project.targets.push_back(std::move(application));
  }

  std::map<std::string, std::vector<std::string>> exports;
  auto requests = project.dependency_requests;
  for (const auto &[git, metadata] : packages)
    for (const auto &requirement : metadata.requirements)
      requests.push_back({source_key(git), requirement});

  std::set<std::string> identities;
  for (const auto &[git, metadata] : packages) {
    const auto &candidate = metadata.candidate;
    const auto id = source_key(git);
    if (!identities.insert(id).second ||
        std::any_of(project.projects.begin(),
                    project.projects.end(),
                    [&](const WorkspaceProject &member) {
                      return member.name == id;
                    }))
      throw std::runtime_error(
          "dependency identity collides with another project");
    bool native = !candidate.source.empty() &&
                  fs::is_regular_file(candidate.source / layout::manifest);
    std::string explicit_system;
    for (const auto &request : requests)
      if (identity(request.requirement) == git &&
          request.requirement.build_system != "auto") {
        if (!explicit_system.empty() &&
            explicit_system != request.requirement.build_system)
          throw std::runtime_error(
              "conflicting build_system requirements for one dependency");
        explicit_system = request.requirement.build_system;
      }

    if (explicit_system == "clutch" && !native)
      throw std::runtime_error(
          "Clutch-native dependency is missing clutch.toml");
    native = native && (explicit_system.empty() || explicit_system == "clutch");
    const bool foreign =
        explicit_system == "cmake" || explicit_system == "make" ||
        candidate.source.empty() ||
        (!native && explicit_system != "header-only" &&
         (fs::is_regular_file(candidate.source / "CMakeLists.txt") ||
          fs::is_regular_file(candidate.source / "Makefile")));
    if (foreign)
      for (const auto &request : requests)
        if (identity(request.requirement) == git) {
          auto requirement = request.requirement;
          requirement.build_system =
              !explicit_system.empty() ? explicit_system
              : !candidate.source.empty() &&
                      !fs::exists(candidate.source / "CMakeLists.txt") &&
                      fs::exists(candidate.source / "Makefile")
                  ? "make"
                  : "cmake";
          project.foreign_requests.push_back(
              {request.owner, git, candidate.source, std::move(requirement)});
        }
    if (!foreign)
      for (const auto &request : requests)
        if (identity(request.requirement) == git &&
            (!request.requirement.libraries.empty() ||
             !request.requirement.build_arguments.empty() ||
             !request.requirement.cmake_options.empty() ||
             !request.requirement.runtime.empty() ||
             (!native && !request.requirement.targets.empty())))
          throw std::runtime_error("native build options require a CMake or "
                                   "Make dependency adapter");
    if (native) {
      for (const auto &request : requests)
        if (identity(request.requirement) == git &&
            !request.requirement.targets.empty())
          throw std::runtime_error("Clutch-native dependency imports select "
                                   "one library with target");
      auto child_options = options;
      child_options.root = candidate.source;
      const auto document =
          toml::parse_file((candidate.source / layout::manifest).u8string());
      auto child =
          load_project_manifest(child_options, document, false, project.build);
      if (document["build"]["runtime"] && child.runtime != project.runtime)
        throw std::runtime_error("dependency runtime conflicts with the "
                                 "consuming project's build.runtime");
      project.inputs.push_back(
          {candidate.source / layout::manifest,
           input_signature(candidate.source / layout::manifest)});
      for (auto &target : child.targets) {
        if (target.type == TargetType::Executable)
          continue;

        target.project_name = id;
        target.local_name = target.name;
        target.name = id + "::" + target.name;
        target.source_root = child.root;
        target.c_standard = child.c_standard;
        target.cxx_standard = child.cxx_standard;
        target.external = true;
        target.build_by_default = false;
        target.includes = merge_includes(child.includes, target.includes);
        target.defines = merge_defines(child.defines, target.defines);
        append(target.pch_c, child.pch_c);
        append(target.pch_cpp, child.pch_cpp);
        for (auto *const links : {&target.links, &target.public_links})
          for (auto &link : *links)
            if (link.find("::") == link.npos &&
                std::none_of(child.dependency_requests.begin(),
                             child.dependency_requests.end(),
                             [&](const Request &request) {
                               return request.requirement.alias == link;
                             }))
              link.insert(0, id + "::");
        exports[git].push_back(target.name);
        append(project.sources, target.sources);
        project.targets.push_back(std::move(target));
      }

      if (!exports.count(git))
        throw std::runtime_error(
            "native dependency has no library targets; select build_system = "
            "\"header-only\" if appropriate");
    }

    if (!candidate.source.empty())
      project.projects.push_back({id, candidate.version, candidate.source});
    Package record{id,
                   git,
                   candidate.revision,
                   candidate.version,
                   candidate.reference,
                   candidate.source,
                   {},
                   {}};
    const auto &package_identity = git;
    const auto requirement =
        std::find_if(requests.begin(),
                     requests.end(),
                     [&](const Request &request) {
                       return identity(request.requirement) == package_identity;
                     });
    record.provider = requirement->requirement.provider;
    if (record.provider != "git") {
      record.git.clear();
      record.revision.clear();
      record.locator = git.substr(record.provider.size() + 1);
    }
    for (const auto &dependency : metadata.requirements)
      append(record.dependencies,
             std::vector{source_key(identity(dependency))});
    for (const auto &request : requests)
      if (identity(request.requirement) == git)
        append(record.requests, std::vector{request_key(request)});
    std::sort(record.dependencies.begin(), record.dependencies.end());
    std::sort(record.requests.begin(), record.requests.end());
    project.packages.push_back(std::move(record));
  }

  std::map<std::string, std::vector<Requirement>> grouped;
  for (const auto &request : requests)
    grouped[request.owner].push_back(request.requirement);
  for (const auto &[owner, requirements] : grouped)
    apply(project, owner, requirements, packages, exports);
}
}  // namespace clutch::dependencies
