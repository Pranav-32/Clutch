/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief Translate target kinds without scattering format strings among
 * backends.
 */
#include "target.hpp"

#include <stdexcept>

namespace clutch {
std::string target_type_name(const TargetType type) {
  switch (type) {
  case TargetType::Executable:
    return "executable";
  case TargetType::StaticLibrary:
    return "static";
  case TargetType::SharedLibrary:
    return "shared";
  }
  throw std::runtime_error("invalid target kind");
}

std::string target_model_type(const TargetType type) {
  switch (type) {
  case TargetType::Executable:
    return "EXECUTABLE";
  case TargetType::StaticLibrary:
    return "STATIC_LIBRARY";
  case TargetType::SharedLibrary:
    return "SHARED_LIBRARY";
  }
  throw std::runtime_error("invalid target kind");
}

TargetType parse_target_type(const std::string &name) {
  if (name == "executable")
    return TargetType::Executable;

  if (name == "static")
    return TargetType::StaticLibrary;

  if (name == "shared")
    return TargetType::SharedLibrary;

  throw std::runtime_error("target.type must be executable, static or shared");
}
}  // namespace clutch
