/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Read pinned upstream Git coordinates without executing vcpkg recipes.
 */
#include "vcpkg.hpp"
#include "manifest.hpp"
#include "platform/files.hpp"
#include "version.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <toml.hpp>

namespace clutch::dependencies {
namespace {
constexpr auto maximum_port_bytes = 1024U * 1024U;

/// Registry metadata is bounded and must not redirect outside the checkout.
std::string contents(const fs::path &path) {
  validate_generated_path(path);
  if (!fs::is_regular_file(path) || fs::file_size(path) > maximum_port_bytes)
    throw std::runtime_error("missing or oversized vcpkg catalog metadata: " +
                             path.u8string());
  const std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  if (stream.bad())
    throw std::runtime_error("cannot read vcpkg catalog metadata");
  return text.str();
}

/// A patched port is not equivalent to an unmodified upstream checkout.
[[noreturn]] void unsupported(const std::string &port,
                              const std::string &reason) {
  throw std::runtime_error("vcpkg catalog port '" + port + "' " + reason +
                           "; import its upstream Git repository explicitly or "
                           "use the port's native package manager");
}
}  // namespace

std::string catalog_repository(const Requirement &requirement) {
  return requirement.root.empty() ? "https://github.com/microsoft/vcpkg.git"
                                  : requirement.root;
}

void resolve_catalog(Requirement &r, const fs::path &catalog) {
  if (r.vcpkg.empty() ||
      !std::all_of(r.vcpkg.begin(),
                   r.vcpkg.end(),
                   [](const unsigned char c) {
                     return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '-';
                   }) ||
      r.vcpkg.front() == '-')
    throw std::runtime_error("invalid vcpkg catalog port name");
  if (!r.features.empty() || !r.triplet.empty())
    unsupported(r.vcpkg, "requires installation-specific features/triplets");
  const auto port = catalog / "ports" / r.vcpkg;
  const auto metadata = nlohmann::json::parse(contents(port / "vcpkg.json"));
  std::string version;
  for (const auto *const field :
       {"version-semver", "version", "version-string", "version-date"})
    if (metadata.contains(field)) {
      version = metadata.at(field).get<std::string>();
      break;
    }
  if (version.empty())
    unsupported(r.vcpkg, "does not declare a catalog version");
  if (!r.version.empty() && r.version != version &&
      !satisfies(version, r.version))
    throw std::runtime_error("pinned vcpkg catalog version " + version +
                             " does not satisfy " + r.version);
  if (metadata.contains("default-features") &&
      !metadata.at("default-features").empty())
    unsupported(r.vcpkg, "has default port features");
  for (const auto &dependency :
       metadata.value("dependencies", nlohmann::json::array()))
    if (!dependency.is_object() || !dependency.value("host", false) ||
        (dependency.value("name", std::string{}) != "vcpkg-cmake" &&
         dependency.value("name", std::string{}) != "vcpkg-cmake-config"))
      unsupported(r.vcpkg, "has port-specific transitive dependencies");

  auto recipe = contents(port / "portfile.cmake");
  // This recognizes literal source declarations; it never evaluates a port.
  recipe = std::regex_replace(recipe, std::regex(R"(#[^\r\n]*)"), "");
  const std::regex fetch(R"(vcpkg_from_(github|git)\s*\(([^\)]*)\))");
  std::smatch match;
  if (!std::regex_search(recipe, match, fetch) ||
      std::regex_search(match.suffix().str(), fetch))
    unsupported(r.vcpkg, "does not have one supported upstream Git fetch");
  const auto method = match[1].str();
  const auto block = match[2].str();
  const std::regex token(R"token("([^"\\]*)"|([^\s]+))token");
  std::vector<std::string> tokens;
  for (std::sregex_iterator item(block.begin(), block.end(), token), end;
       item != end;
       ++item)
    tokens.push_back((*item)[1].matched ? (*item)[1].str() : (*item)[2].str());
  std::map<std::string, std::string> fields;
  for (size_t index = 0; index < tokens.size();) {
    const auto key = tokens[index++];
    if (key == "PATCHES")
      unsupported(r.vcpkg, "requires patches");
    if (key != "OUT_SOURCE_PATH" && key != "REPO" && key != "URL" &&
        key != "REF" && key != "SHA512" && key != "HEAD_REF")
      unsupported(r.vcpkg, "uses unsupported source-fetch options");
    if (index == tokens.size() || !fields.emplace(key, tokens[index]).second)
      unsupported(r.vcpkg, "has an ambiguous source-fetch declaration");
    ++index;
  }
  const auto source = fields.find(method == "github" ? "REPO" : "URL");
  const auto reference = fields.find("REF");
  if (source == fields.end() || reference == fields.end())
    unsupported(r.vcpkg, "does not declare an upstream repository and ref");
  auto ref = reference->second;
  const std::string placeholder = "${VERSION}";
  for (size_t offset = 0; (offset = ref.find(placeholder, offset)) != ref.npos;
       offset += version.size())
    ref.replace(offset, placeholder.size(), version);
  if (ref.empty() || ref.front() == '-' ||
      ref.find_first_of("$; \t\r\n") != ref.npos || source->second.empty() ||
      source->second.front() == '-' ||
      source->second.find_first_of("$;\t\r\n") != ref.npos)
    unsupported(r.vcpkg, "uses computed source coordinates");
  r.git = method == "github" ? "https://github.com/" + source->second + ".git"
                             : source->second;
  r.tag = ref;
  constexpr size_t sha1_digits = 40, sha256_digits = 64;
  if ((ref.size() == sha1_digits || ref.size() == sha256_digits) &&
      std::all_of(ref.begin(), ref.end(), [](const unsigned char c) {
        return std::isxdigit(c) != 0;
      })) {
    r.revision = ref;
    r.tag.clear();
  }
  const toml::table declaration{
      {"dependencies",
       toml::table{{r.alias,
                    toml::table{{"git", r.git},
                                {r.tag.empty() ? "rev" : "tag",
                                 r.tag.empty() ? r.revision : r.tag}}}}}};
  const auto validated = read_requirements(declaration, catalog).front();
  r.git = validated.git;
  r.revision = validated.revision;
  r.tag = validated.tag;
  r.provider = "git";
  r.root.clear();
  // The pinned catalog constraint was checked above; Git now selects an exact
  // upstream ref even when the port uses a date or string version scheme.
  r.catalog_version = r.version;
  r.version.clear();
}
}  // namespace clutch::dependencies
