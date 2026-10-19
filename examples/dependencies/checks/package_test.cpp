/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Verify the selected dependency through its actual public interface.
 */
#include <clutch_example.hpp>
#include <iostream>

/// Return failure when a downloaded, local or installed interface is unusable.
int main() {
  if (!clutch_example::verify()) {
    std::cerr << "Dependency interface check failed\n";
    return 1;
  }

  return 0;
}
