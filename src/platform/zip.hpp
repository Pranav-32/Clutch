/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Write portable source archives without an external compression tool.
 */
#pragma once

#include "core/paths.hpp"
#include <string>
#include <vector>

namespace clutch {
/// ZIP member names use relative UTF-8 paths and forward slashes. A trailing
/// slash denotes a directory. Generated text uses content instead of source.
struct ZipEntry {
  std::string name;
  fs::path source;
  std::string content;
};

/// Stream a classic ZIP using the stored method, UTF-8 names and CRC-32 checks.
/// Refuses existing outputs, redirected/special inputs, ambiguous member names
/// and ZIP64 sizes. Publishes atomically through a sibling staging directory.
/// Input order is normalized; timestamps are fixed for repeatable snapshots.
void write_zip(const fs::path &output, std::vector<ZipEntry> entries);
}  // namespace clutch
