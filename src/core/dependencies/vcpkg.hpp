/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Use supported vcpkg registry entries as upstream source catalog hints.
 */
#pragma once
#include "dependency.hpp"

namespace clutch::dependencies {
/// Resolve a pinned catalog entry to an ordinary Git requirement. No vcpkg
/// executable is discovered, installed or invoked; recipes are never executed.
void resolve_catalog(Requirement &requirement, const fs::path &catalog);
/// Source discovery is optional and shared once per catalog commit.
std::string catalog_repository(const Requirement &requirement);
}  // namespace clutch::dependencies
