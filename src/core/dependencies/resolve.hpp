/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Solve a shared Git package DAG and compose exported native libraries.
 */
#pragma once

namespace clutch {
struct Project;
struct Options;

namespace dependencies {
/// Resolve transitive constraints without compiling. Reject cycles and report
/// conflicting requirement chains; preserve compatible exact locked revisions.
void resolve(Project &project, const Options &options);
}  // namespace dependencies
}  // namespace clutch
