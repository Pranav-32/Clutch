/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Safe code for exercising optional compiler instrumentation policies.
 */
#include <array>
#include <iostream>
#include <numeric>

#ifdef SAFETY_EXAMPLE_THREADS
#include <atomic>
#include <thread>
#endif

int main() {
  constexpr std::array values{12, 14, 16};
  const auto answer = std::accumulate(values.begin(), values.end(), 0);
#ifdef SAFETY_EXAMPLE_THREADS
  // Both workers update the same atomic object without introducing a race.
  std::atomic<int> total{0};
  std::thread first([&] {
    total.fetch_add(answer / 2);
  });
  std::thread second([&] {
    total.fetch_add(answer / 2);
  });
  first.join();
  second.join();
  std::cout << "Safety example: " << total.load() << '\n';
#else
  std::cout << "Safety example: " << answer << '\n';
#endif
}
