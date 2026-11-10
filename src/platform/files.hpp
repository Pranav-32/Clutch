/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Write generated text files only when their content changes, preserving
 * incremental build timestamps.
 */

#pragma once

#include "core/paths.hpp"

#include <set>
#include <string>
#include <system_error>

namespace clutch {
/// Refuse a generated destination that redirects through a link or junction.
/// Call before generating metadata or accepting compiler/linker output paths.
void validate_generated_path(const fs::path &path);

/// Write generated text only when it differs, preserving unchanged output
/// timestamps. Callers validate output ownership before invoking this helper;
/// the writer rejects redirected paths, creates missing parent directories and
/// reports write/close failures. It does not provide a multi-file transaction.
void write_if_changed(const fs::path &path, const std::string &content);

/// Atomically create a lock directory. Returns true when this call created it
/// and false when another holder owns it, or is just releasing it: the holder
/// removing it between creation and the library's directory check (EEXIST on
/// POSIX, "not found" on Windows) and a delete-pending directory on Windows
/// (access denied). A missing parent, a non-directory entry at `path` and any
/// other failure are returned in `error`.
bool try_create_lock_directory(const fs::path &path, std::error_code &error);

/// Create output directories once per generation pass. Generators visit one
/// object per source unit, often thousands sharing a directory; repeating
/// fs::create_directories costs a filesystem round trip for each of them.
/// The cache assumes no other process removes these directories meanwhile.
class DirectoryCreator {
  public:
  /// Create `directory` and missing parents unless already done in this pass.
  void create(const fs::path &directory);

  private:
  std::set<fs::path> created_;
};

/// Encode native filesystem ticks without narrowing wide clock representations.
/// This preserves precision and the complete signed range for local stamps.
std::string file_time_text(fs::file_time_type timestamp);

}  // namespace clutch
