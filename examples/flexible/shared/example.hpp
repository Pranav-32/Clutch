/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Share declarations and named scope values across workspace projects.
 */
#pragma once

inline constexpr int root_scope = 1;
inline constexpr int member_scope = 2;
inline constexpr int target_scope = 3;

/// Read the root library's selected compile scope.
int number();
