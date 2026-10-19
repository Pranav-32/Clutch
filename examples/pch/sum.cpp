/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Compile one of the independent consumers of the prefix header.
 */
#include "sum.hpp"
#include "prefix.hpp"

int sum() {
  const std::vector values{12, 14, 16};
  return std::accumulate(values.begin(), values.end(), 0);
}
