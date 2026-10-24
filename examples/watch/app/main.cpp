/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief A short-lived application reading an explicitly watched resource.
 */
#include <fstream>
#include <iostream>
#include <message.hpp>
#include <string>

int main(const int argc, char **const argv) {
  const auto *const path = argc > 1 ? argv[1] : "assets/message.txt";
  std::ifstream input(path);
  if (!input) {
    std::cerr << "Cannot read resource: " << path << '\n';
    return 1;
  }

  std::string message;
  std::getline(input, message);
  std::cout << message_prefix << (message.empty() ? default_message() : message)
            << '\n';
  for (int index = 2; index < argc; ++index)
    std::cout << "Argument: " << argv[index] << '\n';
}
