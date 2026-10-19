/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Separate declared package requirements from immutable Git resolution.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <utility>
#include <vector>

namespace clutch::dependencies {
/// A dependency alias is local to its declaring project or package.
struct Requirement {
  std::string alias, git, tag, branch, revision, version, target;
  std::string build_system = "auto";
  std::vector<std::string> include_dirs, defines, pch, pch_c;
  // Exactly one source selector is present. Paths are resolved at declaration.
  std::string provider = "git", path, system, vcpkg, root, triplet, baseline,
              locator, cmake_package, catalog_version;
  std::vector<std::string> targets, components, features, libraries, runtime;
  std::vector<std::string> build_arguments;
  std::vector<std::pair<std::string, std::string>> cmake_options;
};

/// Root/member requirements retain ownership when manifests are combined.
struct Request {
  std::string owner;
  Requirement requirement;
};

/// A ref-list candidate is inexpensive; source materialization is
/// demand-driven.
struct Candidate {
  std::string identity, revision, version, reference;
  fs::path source;
};

/// One revision per canonical Git source, shared across all incoming aliases.
struct Package {
  std::string id, git, revision, version, reference;
  fs::path source;
  std::vector<std::string> dependencies, requests;
  std::string provider = "git", locator{};
};

/// Live or installed sources use a provider-qualified identity, never a Git
/// URL.
std::string identity(const Requirement &requirement);
std::string identity(const Package &package);

/// A foreign build remains owned by its native executor. Commands are argv,
/// never shell fragments; artifacts are link prerequisites in the native graph.
struct ForeignBuild {
  std::string id;
  fs::path directory;
  std::vector<std::string> arguments;
  std::vector<fs::path> artifacts;
  fs::path compilation_database{};
};

/// Deferred until the consuming compiler and profile have been selected.
struct ForeignRequest {
  std::string owner, identity;
  fs::path source;
  Requirement requirement;
};

/// Portable spelling for a requirement; also identifies lock compatibility.
std::string request_key(const Request &request);
/// Derive a stable cache namespace; metadata validation detects identity
/// clashes.
std::string source_key(const std::string &git);
}  // namespace clutch::dependencies
