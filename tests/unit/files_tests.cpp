/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Verify generated-file content, timestamp preservation and path safety.
 */
#include "platform/artifacts.hpp"
#include "platform/files.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {
/// Keep invariant checks active in optimized native test executables.
void require(const bool condition, const char *const message) {
  if (!condition)
    throw std::runtime_error(message);
}

/// Round-trip the complete clock range, including libc++'s wide signed rep.
void check_native_file_times() {
  using Time = clutch::fs::file_time_type;
  using Duration = Time::duration;
  const auto now = Time::clock::now();
  for (const auto timestamp : {Time::min(),
                               Time::max(),
                               Time{Duration{0}},
                               Time{Duration{-1}},
                               now,
                               now + Duration{1}}) {
    const auto text = clutch::file_time_text(timestamp);
    require(!text.empty(), "empty filesystem timestamp encoding");
    const bool negative = text.front() == '-';
    Duration::rep parsed = 0;
    for (size_t index = negative ? 1U : 0U; index < text.size(); ++index) {
      const auto digit = text[index] - '0';
      require(digit >= 0 && digit <= 9, "invalid timestamp decimal digit");
      parsed = negative ? parsed * 10 - digit : parsed * 10 + digit;
    }

    require(parsed == timestamp.time_since_epoch().count(),
            "filesystem timestamp lost range or precision");
  }

  require(clutch::file_time_text(Time{Duration{0}}) == "0" &&
              clutch::file_time_text(Time{Duration{-1}}) == "-1",
          "filesystem timestamp sign/zero encoding differs");
  require(clutch::file_time_text(now) !=
              clutch::file_time_text(now + Duration{1}),
          "single native tick was rounded away");
}

/// Read exact bytes independently of the generation helper under test.
std::string read(const clutch::fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "generated file is missing");
  return std::string(std::istreambuf_iterator(input), {});
}

/// Reject the complete removal request before touching any valid earlier path.
void rejects_cleanup(const clutch::fs::path &directory,
                     const std::vector<clutch::fs::path> &artifacts) {
  bool rejected = false;
  try {
    clutch::remove_generated_artifacts(directory, artifacts);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "invalid artifact cleanup was accepted");
}

/// Exercise the same preflight used for Xcode products without requiring Xcode.
void check_artifact_cleanup(const clutch::fs::path &root) {
  namespace fs = clutch::fs;
  const auto directory = root / "owned build";
  const auto product = directory / "app";
  const auto intermediate = directory / "intermediate";
  const auto outside = root / "source.cpp";
  clutch::write_if_changed(product, "product");
  clutch::write_if_changed(intermediate / "app.o", "object");
  clutch::write_if_changed(directory / "configuration.toml", "metadata");
  clutch::write_if_changed(outside, "source");
  for (const auto &invalid : {directory,
                              outside,
                              fs::path("app"),
                              directory / "intermediate" / ".." / "app"}) {
    rejects_cleanup(directory, {product, invalid});
    require(read(product) == "product",
            "preflight removed a valid earlier product");
    require(read(outside) == "source",
            "cleanup modified an input outside the build");
  }

  std::error_code error;
  fs::create_symlink(outside, intermediate / "redirect.cpp", error);
  if (!error) {
    rejects_cleanup(directory, {product, intermediate});
    require(read(product) == "product" && read(outside) == "source",
            "redirect rejection modified existing files");
    fs::remove(intermediate / "redirect.cpp");
  } else
    std::fprintf(stdout,
                 "Symlink cleanup case unavailable: %s\n",
                 error.message().c_str());

  clutch::remove_generated_artifacts(
      directory,
      {product, intermediate, directory / "missing"});
  require(!fs::exists(product) && !fs::exists(intermediate),
          "cleanup retained requested artifacts");
  require(read(directory / "configuration.toml") == "metadata" &&
              read(outside) == "source",
          "cleanup removed unlisted metadata or inputs");
}

/// Competing writers repeatedly take and release one lock directory. Every
/// attempt must either acquire it or see it held: a holder removing the
/// directory mid-check (EEXIST without a directory on POSIX) is not an error.
void check_lock_contention(const clutch::fs::path &root) {
  const auto lock = root / "contended.lock";
  constexpr auto writers = 4;
  constexpr auto acquisitions = 400;
  std::atomic<int> inside{0};
  std::atomic<bool> failed{false};
  std::string failure;
  std::mutex failure_mutex;
  std::vector<std::thread> threads;
  threads.reserve(writers);
  for (auto writer = 0; writer < writers; ++writer)
    threads.emplace_back([&] {
      for (auto count = 0; count < acquisitions && !failed; ++count) {
        std::error_code error;
        while (!clutch::try_create_lock_directory(lock, error)) {
          if (error) {
            const std::lock_guard guard(failure_mutex);
            failure = error.message();
            failed = true;
            return;
          }
          std::this_thread::yield();
        }
        if (++inside != 1)
          failed = true;
        --inside;
        clutch::fs::remove(lock, error);
      }
    });
  for (auto &thread : threads)
    thread.join();
  if (failed)
    throw std::runtime_error(
        failure.empty() ? "two writers held one lock directory"
                        : "lock contention reported an error: " + failure);
  require(!clutch::fs::exists(lock), "contended lock directory remained");

  // A regular file where the lock belongs is an obstruction, not a holder.
  std::ofstream(lock) << "not a lock directory\n";
  std::error_code error;
  require(!clutch::try_create_lock_directory(lock, error) && error,
          "a non-directory lock path was treated as a lock holder");
  clutch::fs::remove(lock);
  require(
      !clutch::try_create_lock_directory(root / "missing" / "x.lock", error) &&
          error,
      "a lock below a missing parent was treated as held");
}
}  // namespace

/// Keep all fixtures inside CTest's uniquely named build-artifact directory.
int main(const int argc, char **const argv) {
  namespace fs = clutch::fs;
  try {
    require(argc == 2, "expected a fixture output directory");
    check_native_file_times();
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::absolute(fs::u8path(argv[1])) / std::to_string(stamp);
    fs::create_directories(root.parent_path());
    require(fs::create_directory(root), "fixture directory already exists");
    const auto output = root / "nested" / "generated.txt";
    clutch::write_if_changed(output, "initial content\n");
    require(read(output) == "initial content\n", "generated bytes differ");

    // Set an old timestamp explicitly so coarse filesystems cannot conceal a
    // needless rewrite; no clock sleeps or future-dated prerequisites are used.
    constexpr auto timestamp_age = std::chrono::hours(24);
    fs::last_write_time(output,
                        fs::file_time_type::clock::now() - timestamp_age);
    const auto original = fs::last_write_time(output);
    clutch::write_if_changed(output, "initial content\n");
    require(fs::last_write_time(output) == original,
            "unchanged content rewrote its output timestamp");

    clutch::write_if_changed(output, "changed content\n");
    require(read(output) == "changed content\n",
            "changed content was not written");

    bool rejected = false;
    try {
      clutch::write_if_changed(output.parent_path(),
                               "invalid directory output");
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected && read(output) == "changed content\n",
            "a directory output damaged existing files");
    check_artifact_cleanup(root);

    check_lock_contention(root);

    // Generators create shared object directories once per pass.
    clutch::DirectoryCreator directories;
    const auto objects = root / "objects" / "deep" / "target";
    directories.create(objects);
    require(fs::is_directory(objects), "nested output directory missing");
    fs::remove(objects);
    directories.create(objects);
    require(!fs::exists(objects),
            "a directory already created in this pass was created again");
    directories.create(objects / "sibling");
    require(fs::is_directory(objects / "sibling"),
            "a new directory below a cached one was not created");
    clutch::DirectoryCreator next_pass;
    next_pass.create(objects);
    require(fs::is_directory(objects), "a new pass reused another pass' cache");
    fs::remove_all(root);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
  return 0;
}
