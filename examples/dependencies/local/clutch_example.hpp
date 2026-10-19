/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Consume a header directory, a native library and an installed package.
 */
#pragma once

#include <clutch_demo_sdk.hpp>
#include <demo_headers.hpp>
#include <iostream>
#include <number.hpp>

namespace clutch_example {
/// Compare independently supplied header and compiled-library values.
inline bool verify() {
  return demo_number::answer() == demo_headers::expected_answer &&
         clutch_demo_sdk::version == "1.0.0";
}

/// All interfaces arrive through dependencies; no manual linker flags are used.
inline int run(int, char **) {
  std::cout << "Local packages: " << demo_number::answer()
            << "; installed SDK: " << clutch_demo_sdk::version << '\n';
  return verify() ? 0 : 1;
}
}  // namespace clutch_example
