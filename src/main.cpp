/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Enter the standalone executable and delegate command handling to the
 * app module.
 */

#include "app/cli.hpp"

/// Preserve the workflow's exit status for calling shells and automation.
int main(const int argc, char *const *const argv) {
  return clutch::run_cli(argc, argv);
}