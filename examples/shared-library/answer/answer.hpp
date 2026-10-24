/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief A public DLL API with explicit Windows export/import annotations.
 */
#pragma once

#ifdef _WIN32
#ifdef ANSWER_BUILD
#define ANSWER_API __declspec(dllexport)
#else
#define ANSWER_API __declspec(dllimport)
#endif
#else
#define ANSWER_API
#endif

/// Return the shared implementation's answer without exposing private headers.
ANSWER_API int shared_answer() noexcept;
