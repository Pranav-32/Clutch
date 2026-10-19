/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Link an installed CUDA runtime from ordinary host C++ source.
 */
#pragma once
#include <cuda_runtime_api.h>
#include <iostream>

namespace clutch_example {
/// Reference a runtime symbol without executing a kernel or initializing a GPU.
inline bool verify() {
  const volatile auto entry_point = &cudaRuntimeGetVersion;
  return entry_point != nullptr;
}

/// A static imported target avoids guessing version-specific runtime DLL names.
inline int run(int, char **) {
  std::cout << "Installed CUDA Toolkit: " << CUDART_VERSION << '\n';
  return verify() ? 0 : 1;
}
}  // namespace clutch_example
