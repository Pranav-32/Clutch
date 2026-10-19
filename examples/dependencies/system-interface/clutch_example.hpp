/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Consume explicit installed headers without package-specific discovery.
 */
#pragma once
#include <clutch_demo_sdk.hpp>
#include <iostream>

namespace clutch_example {
/// An explicit include contract supports SDKs without build-system metadata.
inline bool verify() { return clutch_demo_sdk::version == "1.0.0"; }

/// The logical system identity is independent of the SDK vendor or toolchain.
inline int run(int, char **) {
  std::cout << "System interface: " << clutch_demo_sdk::version << '\n';
  return verify() ? 0 : 1;
}
}  // namespace clutch_example
