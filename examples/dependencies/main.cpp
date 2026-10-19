/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Run the dependency example selected by its manifest's include path.
 */
#include <clutch_example.hpp>

/// Each example supplies its own small adapter for this shared application.
int main(const int argc, char **const argv) {
  return clutch_example::run(argc, argv);
}
