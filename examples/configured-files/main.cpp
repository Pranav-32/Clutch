/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Consume an automatically exposed generated include directory.
 */
#include <build_info.hpp>
#include <iostream>

int main() {
  std::cout << build_info::name << " " << build_info::version
            << "; base=" << build_info::base_version
            << "; API=" << build_info::api_level << '\n'
            << build_info::message << '\n'
            << "Literal token: " << build_info::literal_token << '\n';
}
