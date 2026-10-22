/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Keep network resolution, frozen pins and source-cache validation
 * aligned.
 */

#include "git.hpp"
#include "core/project_layout.hpp"
#include "git_submodules.hpp"
#include "platform/capture.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include "platform/terminal.hpp"
#include "version.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <toml.hpp>
#include <utility>

namespace clutch::dependencies {
namespace {
constexpr auto cache_schema = 1;
// Git supports SHA-1 and SHA-256 object formats. Never accept abbreviated IDs.
constexpr size_t sha1_digits = 40, sha256_digits = 64;
constexpr auto maximum_lock_bytes = 1024U * 1024U;

/// Accept only complete lowercase object IDs from Git or a generated lock.
bool commit_id(const std::string &text) {
  return (text.size() == sha1_digits || text.size() == sha256_digits) &&
         std::all_of(text.begin(), text.end(), [](const unsigned char value) {
           return (value >= '0' && value <= '9') ||
                  (value >= 'a' && value <= 'f');
         });
}

/// Reject missing or mistyped identity fields before deriving cache paths.
std::string required(const toml::table &table, const char *const key) {
  if (const auto value = table[key].value<std::string>(); value)
    return *value;

  throw std::runtime_error(std::string("invalid dependency record: ") + key);
}

/// Copy lock strings while preserving canonical edge and request order.
std::vector<std::string> list(const toml::table &table, const char *const key) {
  std::vector<std::string> result;
  const auto *const values = table[key].as_array();
  if (!values)
    throw std::runtime_error(std::string("invalid dependency list: ") + key);

  for (const auto &value : *values) {
    if (const auto item = value.value<std::string>(); item)
      result.push_back(*item);
    else
      throw std::runtime_error("dependency lock lists must contain strings");
  }

  return result;
}

/// Cache destinations are derived entirely from checked identities and hashes.
fs::path checkout(const fs::path &cache, const Candidate &candidate) {
  if (!commit_id(candidate.revision))
    throw std::runtime_error("invalid dependency commit ID");

  return cache / "deps" / source_key(candidate.identity) / candidate.revision;
}

/// Git's line-oriented output may use LF or CRLF on the current host.
std::string trim(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    text.pop_back();
  return text;
}

/// Validate completion/ownership records before accepting or recovering
/// sources.
void validate_record(const fs::path &path, const Candidate &candidate) {
  validate_generated_path(path);
  if (!fs::is_regular_file(path) || fs::file_size(path) > maximum_lock_bytes)
    throw std::runtime_error("invalid dependency cache record: " +
                             path.u8string());

  if (const auto record = toml::parse_file(path.u8string());
      required(record, "git") != candidate.identity ||
      required(record, "rev") != candidate.revision)
    throw std::runtime_error(
        "dependency cache identity does not match its metadata");
}

/// Best-effort rollback of this operation's unpublished checkout. Git object
/// files can be read-only on Windows; change permissions only on verified
/// regular entries, never on symlink targets or an already published checkout.
bool discard_checkout(const fs::path &stage) noexcept {
  try {
    validate_generated_path(stage);
    std::error_code error;
    for (fs::recursive_directory_iterator entry(stage, error), end;
         entry != end && !error;
         entry.increment(error)) {
      const auto status = entry->symlink_status(error);
      if (error)
        break;

      if (fs::is_symlink(status)) {
        entry.disable_recursion_pending();
        continue;
      }

      validate_generated_path(entry->path());
      if (fs::is_regular_file(status))
        fs::permissions(entry->path(),
                        fs::perms::owner_write,
                        fs::perm_options::add,
                        error);
    }

    fs::remove_all(stage, error);
    return !error;
  } catch (...) {
    // Preserve the original fetch/checkout error; a retained stage is reported
    // explicitly by the caller and on subsequent attempts to configure.
    return false;
  }
}
}  // namespace

std::vector<Package> read_pins(const fs::path &root) {
  const auto path = root / layout::lock_file;
  validate_generated_path(path);
  if (!fs::exists(path))
    return {};

  if (!fs::is_regular_file(path) || fs::file_size(path) > maximum_lock_bytes)
    throw std::runtime_error(
        "clutch.lock must be a regular metadata file of at most one MiB");
  const auto document = toml::parse_file(path.u8string());
  const auto schema = document["version"].value_or(0);
  if (schema != 1 && schema != 2 && schema != 3)
    throw std::runtime_error("unsupported clutch.lock version");

  std::vector<Package> result;
  if (const auto *const node = document.get("dependency")) {
    const auto *const packages = node->as_array();
    if (!packages || (schema != 2 && schema != 3))
      throw std::runtime_error("invalid Git dependency lock layout");

    std::set<std::string> seen;
    for (const auto &value : *packages) {
      const auto *const table = value.as_table();
      if (!table)
        throw std::runtime_error("invalid dependency lock record");

      auto fields = std::set<std::string>{"git",
                                          "id",
                                          "rev",
                                          "version",
                                          "ref",
                                          "dependencies",
                                          "requests"};
      if (schema == 3)
        fields.insert({"provider", "source"});
      for (const auto &[key, setting] : *table) {
        (void)setting;
        if (!fields.count(std::string(key.str())))
          throw std::runtime_error("unknown dependency lock field: " +
                                   std::string(key.str()));
      }

      Package package;
      package.provider = schema == 3 ? required(*table, "provider") : "git";
      if (package.provider != "git" && package.provider != "path" &&
          package.provider != "system")
        throw std::runtime_error("unknown dependency lock provider");
      if (package.provider == "git")
        package.git = required(*table, "git");
      else
        package.locator = required(*table, "source");
      package.id = required(*table, "id");
      package.revision = required(*table, "rev");
      package.version = required(*table, "version");
      package.reference = required(*table, "ref");
      package.dependencies = list(*table, "dependencies");
      package.requests = list(*table, "requests");
      if (identity(package).empty() || package.reference.empty() ||
          package.version.empty() || package.requests.empty() ||
          package.id != source_key(identity(package)) ||
          (package.provider == "git" &&
           (package.git.empty() || !commit_id(package.revision))) ||
          (package.provider != "git" &&
           (package.locator.empty() || !package.revision.empty())) ||
          !seen.insert(identity(package)).second)
        throw std::runtime_error(
            "duplicate or invalid dependency lock identity");
      if (package.provider == "git")
        package.source = root / layout::environment / "deps" / package.id /
                         package.revision / "source";
      else if (package.provider == "path")
        package.source =
            fs::absolute(root / fs::u8path(package.locator)).lexically_normal();
      result.push_back(std::move(package));
    }
  }

  // Reject dangling references and cycles before trusting a frozen package
  // graph.
  std::map<std::string, const Package *> identities;
  for (const auto &package : result)
    if (!identities.emplace(package.id, &package).second)
      throw std::runtime_error("duplicate dependency lock ID");

  enum class Visit {
    New,
    Active,
    Done
  };
  std::map<std::string, Visit> states;
  std::function<void(const std::string &)> visit = [&](const std::string &id) {
    if (states[id] == Visit::Active)
      throw std::runtime_error("dependency lock contains a cycle");

    if (states[id] == Visit::Done)
      return;

    const auto found = identities.find(id);
    if (found == identities.end())
      throw std::runtime_error("dependency lock references an unknown package");

    states[id] = Visit::Active;
    std::set<std::string> edges;
    for (const auto &dependency : found->second->dependencies) {
      if (!edges.insert(dependency).second)
        throw std::runtime_error("duplicate dependency lock edge");

      visit(dependency);
    }

    states[id] = Visit::Done;
  };
  for (const auto &[id, package] : identities) {
    (void)package;
    visit(id);
  }

  return result;
}

GitStore::GitStore(const fs::path &root,
                   const bool offline,
                   const bool locked,
                   std::vector<Package> pins)
    : root_(root),
      cache_(root / layout::environment),
      offline_(offline),
      locked_(locked),
      executable_(find_program("git")),
      pins_(std::move(pins)) {
  if (executable_.empty())
    throw std::runtime_error(
        "Git dependency resolution requires an installed git executable");
  const auto marker = cache_ / "dependency-cache.toml";
  validate_generated_path(marker);
  if (fs::exists(cache_) && !fs::is_empty(cache_) &&
      !fs::is_regular_file(marker))
    throw std::runtime_error(
        "refusing an unowned nonempty .clutch dependency directory");
  if (fs::exists(marker) &&
      toml::parse_file(marker.u8string())["schema"].value_or(0) != cache_schema)
    throw std::runtime_error("unsupported .clutch dependency cache");

  fs::create_directories(cache_);
  write_if_changed(marker,
                   "# Clutch project-local dependency cache.\nschema = 1\n");
  const auto writer = cache_ / "resolve.lock";
  validate_generated_path(writer);
  if (!fs::create_directory(writer))
    throw std::runtime_error("dependency resolution is already active; inspect "
                             ".clutch/resolve.lock if interrupted");
  writer_ = writer;
}

GitStore::~GitStore() {
  if (!writer_.empty()) {
    std::error_code ignored;
    fs::remove(writer_, ignored);
  }
}

std::string GitStore::git(const fs::path &directory,
                          const std::vector<std::string> &arguments) const {
  std::vector<std::string> command{executable_, "-c", "core.quotePath=false"};
#ifdef _WIN32
  // Nested module object stores can exceed MAX_PATH even for short source
  // filenames. This applies only to this Git process and its children.
  command.insert(command.end(), {"-c", "core.longpaths=true"});
#endif
  command.insert(command.end(), arguments.begin(), arguments.end());
  const auto result = capture(command, directory);
  if (result.status)
    throw std::runtime_error("Git dependency operation failed (exit " +
                             std::to_string(result.status) + ")");
  return trim(result.text);
}

bool GitStore::accepts(const Candidate &candidate,
                       const Request &request) const {
  const auto &r = request.requirement;
  if (!r.revision.empty() && candidate.revision != r.revision)
    return false;

  if (const auto pin = std::find_if(pins_.begin(),
                                    pins_.end(),
                                    [&](const Package &package) {
                                      return package.git ==
                                                 candidate.identity &&
                                             std::find(package.requests.begin(),
                                                       package.requests.end(),
                                                       request_key(request)) !=
                                                 package.requests.end();
                                    });
      pin != pins_.end()) {
    const bool immutable = !r.tag.empty() || !r.revision.empty();
    if (immutable && pin->revision != candidate.revision)
      return false;

    // Mutable branches must consult freshly advertised refs during an update.
    if (pin->revision == candidate.revision && (preserve_pins_ || immutable))
      return candidate.version.empty() ||
             satisfies(candidate.version, r.version);
  }

  const auto refs = remotes_.find(candidate.identity);
  for (const auto &reference :
       {r.tag.empty() ? std::string{} : "refs/tags/" + r.tag,
        r.branch.empty() ? std::string{} : "refs/heads/" + r.branch}) {
    if (reference.empty())
      continue;

    if (refs != remotes_.end()) {
      if (const auto found = refs->second.find(reference);
          found == refs->second.end() || found->second != candidate.revision)
        return false;
    } else if (candidate.reference != reference)
      return false;
  }

  return candidate.version.empty() || satisfies(candidate.version, r.version);
}

std::vector<Candidate>
GitStore::candidates(const std::string &source,
                     const std::vector<Request> &requests) {
  std::vector<Candidate> result;
  const bool fixed =
      std::any_of(requests.begin(), requests.end(), [](const Request &request) {
        return !request.requirement.tag.empty() ||
               !request.requirement.revision.empty();
      });
  const bool all_fixed =
      std::all_of(requests.begin(), requests.end(), [](const Request &request) {
        return !request.requirement.tag.empty() ||
               !request.requirement.revision.empty();
      });
  const auto allowed = [&](const Candidate &candidate) {
    return std::all_of(requests.begin(),
                       requests.end(),
                       [&](const Request &request) {
                         return accepts(candidate, request);
                       });
  };
  const auto add_pins = [&] {
    for (const auto &pin : pins_)
      if (pin.git == source) {
        Candidate candidate{pin.git,
                            pin.revision,
                            pin.version,
                            pin.reference,
                            pin.source};
        const bool known = std::all_of(
            requests.begin(),
            requests.end(),
            [&](const Request &request) {
              return std::find(pin.requests.begin(),
                               pin.requests.end(),
                               request_key(request)) != pin.requests.end();
            });
        if (known && allowed(candidate) && (preserve_pins_ || fixed))
          result.push_back(std::move(candidate));
      }
  };
  add_pins();

  if (locked_ || (!result.empty() &&
                  (!explore_ || (fixed && (preserve_pins_ || all_fixed)))))
    return result;

  if (!remotes_.count(source)) {
    if (offline_)
      return result;

    // Pull-request/notes namespaces are irrelevant to package selection and
    // can dwarf a repository's actual branches and release tags.
    const auto listing = git(root_,
                             {"ls-remote",
                              "--symref",
                              "--",
                              source,
                              "HEAD",
                              "refs/heads/*",
                              "refs/tags/*"});
    auto &refs = remotes_[source];
    std::istringstream lines(listing);
    std::string line;
    std::map<std::string, std::string> peeled;
    while (std::getline(lines, line)) {
      const auto separator = line.find('\t');
      if (separator == line.npos || !commit_id(line.substr(0, separator)))
        continue;

      const auto revision = line.substr(0, separator);
      auto reference = trim(line.substr(separator + 1));
      constexpr std::string_view peeled_suffix = "^{}";
      if (reference.size() >= peeled_suffix.size() &&
          reference.compare(reference.size() - peeled_suffix.size(),
                            peeled_suffix.size(),
                            peeled_suffix) == 0)
        peeled.emplace(
            reference.substr(0, reference.size() - peeled_suffix.size()),
            revision);
      else
        refs.emplace(std::move(reference), revision);
    }

    for (const auto &[name, revision] : peeled)
      refs[name] = revision;
  }

  // Ref discovery can make a previously unknown branch/tag intersection valid.
  add_pins();

  const auto &refs = remotes_.at(source);
  const bool constrained =
      std::any_of(requests.begin(), requests.end(), [](const Request &request) {
        const auto &r = request.requirement;
        return !r.tag.empty() || !r.branch.empty() || !r.revision.empty() ||
               !r.version.empty();
      });
  for (const auto &[reference, revision] : refs) {
    constexpr std::string_view tag_prefix = "refs/tags/";
    const bool tag = reference.rfind(tag_prefix, 0) == 0;
    const bool version_only =
        std::any_of(requests.begin(),
                    requests.end(),
                    [](const Request &request) {
                      const auto &r = request.requirement;
                      return !r.version.empty() && r.tag.empty() &&
                             r.branch.empty() && r.revision.empty();
                    });
    if (version_only && !tag)
      continue;

    if ((!constrained && reference != "HEAD") ||
        (constrained && !tag && reference != "HEAD" &&
         std::none_of(
             requests.begin(),
             requests.end(),
             [&, &reference_value = reference](const Request &request) {
               return !request.requirement.branch.empty() &&
                      reference_value ==
                          "refs/heads/" + request.requirement.branch;
             })))
      continue;

    const auto version =
        tag && parse_version(reference.substr(tag_prefix.size()))
            ? reference.substr(tag_prefix.size())
            : std::string{};
    if (version_only && version.empty())
      continue;

    Candidate candidate{source, revision, version, reference, {}};
    if (allowed(candidate))
      result.push_back(std::move(candidate));
  }

  for (const auto &request : requests) {
    if (!request.requirement.revision.empty()) {
      Candidate candidate{source,
                          request.requirement.revision,
                          {},
                          request.requirement.revision,
                          {}};
      if (allowed(candidate))
        result.push_back(std::move(candidate));
    }
  }

  std::stable_sort(result.begin(),
                   result.end(),
                   [](const Candidate &left, const Candidate &right) {
                     const auto a = parse_version(left.version),
                                b = parse_version(right.version);
                     if (a && b)
                       return compare(*a, *b) > 0;

                     return a.has_value() > b.has_value();
                   });
  if (preserve_pins_ || fixed)
    std::stable_partition(result.begin(),
                          result.end(),
                          [&](const Candidate &candidate) {
                            return std::any_of(
                                pins_.begin(),
                                pins_.end(),
                                [&](const Package &pin) {
                                  return pin.git == candidate.identity &&
                                         pin.revision == candidate.revision;
                                });
                          });
  std::set<std::pair<std::string, std::string>> seen;
  result.erase(
      std::remove_if(
          result.begin(),
          result.end(),
          [&](const Candidate &candidate) {
            return !allowed(candidate) ||
                   !seen.emplace(candidate.revision, candidate.version).second;
          }),
      result.end());
  return result;
}

void GitStore::explore_alternatives(const bool preserve_pins) {
  explore_ = true;
  preserve_pins_ = preserve_pins;
}

void GitStore::validate_checkout(Candidate &candidate,
                                 const fs::path &directory) const {
  candidate.source = fs::canonical(directory / "source");
  if (candidate.source != directory / "source" ||
      git(candidate.source,
          {"rev-parse", "--verify", "--quiet", "HEAD^{commit}"}) !=
          candidate.revision ||
      git(candidate.source, {"remote", "get-url", "origin"}) !=
          candidate.identity ||
      !git(candidate.source,
           {"status",
            "--porcelain",
            "--untracked-files=all",
            "--ignore-submodules=none"})
           .empty())
    throw std::runtime_error("dependency checkout was modified; restore or "
                             "remove its project-local cache");

  prepare_git_submodules(candidate.source,
                         offline_,
                         [&](const fs::path &repository,
                             const std::vector<std::string> &arguments) {
                           return git(repository, arguments);
                         });
}

Candidate GitStore::materialize(Candidate candidate) {
  const auto destination = checkout(cache_, candidate);
  const auto metadata = destination / "resolution.toml";
  // Keep ownership outside the source tree so a failed rollback cannot remove
  // the evidence needed to recover it. Published cache paths remain unchanged.
  const auto pending =
      destination.parent_path() / (candidate.revision + ".pending.toml");
  validate_generated_path(metadata);
  validate_generated_path(pending);
  bool verified = false;
  if (!fs::exists(metadata)) {
    if (fs::exists(destination) && !fs::is_regular_file(pending))
      throw std::runtime_error(
          "incomplete dependency checkout has no ownership record; inspect " +
          destination.u8string());

    if (fs::exists(pending)) {
      validate_record(pending, candidate);
      if (fs::exists(destination)) {
        try {
          validate_checkout(candidate, destination);
          verified = true;
        } catch (const std::runtime_error &) {
          if (offline_)
            throw std::runtime_error(
                "incomplete dependency checkout cannot be recovered offline: " +
                destination.u8string());

          // Only a matching private ownership record authorizes removal. A
          // completed checkout is never repaired by deleting or overwriting it.
          if (!discard_checkout(destination))
            throw std::runtime_error(
                "cannot recover incomplete dependency checkout; close programs "
                "using its files: " +
                destination.u8string());
        }
      }
    }

    if (!verified) {
      if (offline_)
        throw std::runtime_error(
            "dependency checkout is unavailable offline: " +
            source_key(candidate.identity));
      status("Fetching", candidate.identity + " @ " + candidate.reference);
      fs::create_directories(destination.parent_path());
      const toml::table record{{"git", candidate.identity},
                               {"rev", candidate.revision},
                               {"ref", candidate.reference},
                               {"version", candidate.version}};
      std::ostringstream text;
      text << record << '\n';
      write_if_changed(pending, text.str());
      validate_generated_path(metadata);
      if (!fs::create_directory(destination))
        throw std::runtime_error(
            "dependency checkout destination already exists");

      try {
        const auto source = destination / "source";
        git(root_,
            {"init",
             "--quiet",
             candidate.revision.size() == sha256_digits
                 ? "--object-format=sha256"
                 : "--object-format=sha1",
             source.u8string()});
        git(source, {"remote", "add", "origin", candidate.identity});
        // Fetch the selected object, including when a formerly locked branch
        // moved.
        git(source,
            {"fetch",
             "--quiet",
             "--depth=1",
             "--no-tags",
             "origin",
             candidate.revision});
        if (const auto actual =
                git(source, {"rev-parse", "FETCH_HEAD^{commit}"});
            actual != candidate.revision)
          throw std::runtime_error(
              "Git ref changed during dependency resolution; configure again");
        git(source,
            {"-c",
             "advice.detachedHead=false",
             "checkout",
             "--quiet",
             "--detach",
             candidate.revision});
        validate_checkout(candidate, destination);
        verified = true;
      } catch (...) {
        if (discard_checkout(destination)) {
          std::error_code ignored;
          fs::remove(pending, ignored);
        } else {
          std::fprintf(
              stderr,
              "warning: incomplete dependency checkout retained at %s; "
              "configure again after closing programs using its files\n",
              destination.u8string().c_str());
        }
        throw;
      }
    }

    // Moving the checkout itself fails on Windows when any descendant has an
    // open non-delete-sharing handle. Commit only the small, closed record.
    // Keep verified sources and ownership metadata if this final step fails.
    validate_generated_path(metadata);
    validate_generated_path(pending);
    if (fs::exists(metadata))
      throw std::runtime_error("dependency completion record already exists");

    std::error_code error;
    fs::rename(pending, metadata, error);
    if (error)
      throw fs::filesystem_error(
          "cannot complete Git checkout; verified sources are retained for "
          "the next configure",
          pending,
          metadata,
          error);
  }

  validate_record(metadata, candidate);
  if (!verified)
    validate_checkout(candidate, destination);
  return candidate;
}
}  // namespace clutch::dependencies
