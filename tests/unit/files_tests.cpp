/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Check staged-directory publication, destination safety and Windows
 * sharing.
 */
#include "platform/files.hpp"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#include <future>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
/// Keep invariant checks active in optimized native test executables.
void require(const bool condition, const char *const message) {
  if (!condition)
    throw std::runtime_error(message);
}
}  // namespace

/// Publish only uniquely created fixtures under CTest's build artifact
/// directory.
int main(const int argc, char **const argv) {
  namespace fs = clutch::fs;
  try {
    require(argc == 2, "expected a fixture output directory");
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::absolute(fs::u8path(argv[1])) / std::to_string(stamp);
    fs::create_directories(root.parent_path());
    require(fs::create_directory(root), "fixture directory already exists");
    const auto source = root / "stage";
    const auto destination = root / "published";
    clutch::write_if_changed(source / "payload.txt", "complete data\n");

#ifdef _WIN32
    auto *const handle = CreateFileW(source.c_str(),
                                     FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS,
                                     nullptr);
    require(handle != INVALID_HANDLE_VALUE, "cannot lock the staged directory");
    // A non-delete-sharing handle reproduces scanners holding a Git checkout.
    constexpr auto hold_duration = std::chrono::milliseconds(100);
    auto release = std::async(std::launch::async, [handle, hold_duration] {
      std::this_thread::sleep_for(hold_duration);
      CloseHandle(handle);
    });
#endif
    clutch::publish_directory(source, destination);
    require(!fs::exists(source) &&
                fs::is_regular_file(destination / "payload.txt"),
            "publication left an incomplete source/destination");
#ifdef _WIN32
    release.get();
#endif

    const auto second = root / "another-stage";
    clutch::write_if_changed(second / "payload.txt", "replacement\n");
    bool rejected = false;
    try {
      clutch::publish_directory(second, destination);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected && fs::exists(second / "payload.txt"),
            "publication overwrote an existing destination");
    fs::remove_all(root);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
  return 0;
}
