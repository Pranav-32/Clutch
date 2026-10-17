/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Parse command-line options and run configure, build, run and test
 * workflows.
 */

#pragma once

namespace clutch {
/// Parse commands and return the command, compiler or launched program's exit
/// status.
int run_cli(int argc, char *const *const argv);
} // namespace clutch