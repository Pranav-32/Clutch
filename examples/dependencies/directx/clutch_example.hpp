/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Verify installed Windows SDK headers and the Direct3D import library.
 */
#pragma once
#include <d3d12.h>
#include <iostream>

namespace clutch_example {
/// Retain an SDK symbol reference without creating a device or requiring a GPU.
inline bool verify() {
  const volatile auto entry_point = &D3D12CreateDevice;
  return entry_point != nullptr;
}

/// The Windows SDK is consumed in place; no SDK files are installed by Clutch.
inline int run(int, char **) {
  std::cout << "Installed DirectX: d3d12\n";
  return verify() ? 0 : 1;
}
}  // namespace clutch_example
