/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */

/**
 * @file
 * @brief Provide the shared standard-filesystem namespace alias for internal
 * modules.
 */

#pragma once

#include <filesystem>

namespace clutch {
namespace fs = std::filesystem;
}  // namespace clutch
