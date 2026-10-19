/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Check archive path, publication and resource-bound contracts.
 */
#include "platform/zip.hpp"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace {
namespace fs = clutch::fs;

/// Invalid archives must fail before publishing the destination.
void rejects(const fs::path &output, std::vector<clutch::ZipEntry> entries) {
  try {
    clutch::write_zip(output, std::move(entries));
  } catch (const std::exception &) {
    if (fs::exists(output))
      throw std::runtime_error("invalid ZIP was published");
    return;
  }
  throw std::runtime_error("invalid ZIP was accepted");
}

/// Preserve a failed assertion even if filesystem cleanup also fails.
void discard(const fs::path &root) noexcept {
  try {
    fs::remove_all(root);
  } catch (...) {
    std::fputs("ZIP fixture cleanup failed\n", stderr);
  }
}
}  // namespace

/// All cleanup stays inside a newly created, uniquely named test directory.
int main(const int argc, char **const argv) {
  fs::path root;
  bool owned = false;
  try {
    if (argc != 2)
      throw std::runtime_error("expected artifact parent directory");
    root = fs::absolute(fs::u8path(argv[1])) /
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root.parent_path());
    owned = fs::create_directory(root);
    if (!owned)
      throw std::runtime_error("ZIP fixture already exists");
    const auto output = root / "invalid.zip";
    for (const auto &name : {std::string("../escape"),
                             std::string("/absolute"),
                             std::string("a/../b"),
                             std::string("a\\b"),
                             std::string("a//b"),
                             std::string("CON.txt"),
                             std::string("aux"),
                             std::string("LPT1"),
                             std::string("trailing. "),
                             std::string("bad\xc0\xaf")})
      rejects(output, {{name, {}, "data"}});

    rejects(output, {{"file", {}, "one"}, {"FILE", {}, "two"}});
    rejects(output, {{"file", {}, "one"}, {"file/child", {}, "two"}});
    rejects(output, {{"directory/", {}, {}}, {"DIRECTORY", {}, "two"}});
    rejects(output, {});
    constexpr auto zip64_member_sentinel = size_t{65535};
    rejects(output, std::vector<clutch::ZipEntry>(zip64_member_sentinel));

    const auto valid = root / "valid.zip";
    clutch::write_zip(valid, {{"empty/", {}, {}}, {"hello.txt", {}, "hello"}});
    const auto size = fs::file_size(valid);
    bool refused = false;
    try {
      clutch::write_zip(valid, {{"replacement", {}, "replacement"}});
    } catch (const std::exception &) {
      refused = true;
    }
    if (!refused || fs::file_size(valid) != size)
      throw std::runtime_error("existing archive was not preserved");
    fs::remove_all(root);
    owned = false;
    std::puts("PASS: ZIP path and publication contracts");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    if (owned)
      discard(root);
    return 1;
  }
}
