/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */
/**
 * @file
 * @brief Run upstream Makefiles in an isolated output directory with explicit
 * usage.
 */
#include "make.hpp"
#include "core/project.hpp"
#include "core/project_layout.hpp"
#include "platform/files.hpp"
#include "platform/process.hpp"
#include "toolchains/toolchain.hpp"
#include <stdexcept>

namespace clutch::dependencies {
namespace {
/// Placeholders substitute argv values, never executable shell text.
std::string
expand(std::string value, const fs::path &source, const fs::path &build) {
  for (const auto &[key, replacement] :
       {std::pair{std::string("{source}"), source.u8string()},
        std::pair{std::string("{build}"), build.u8string()}})
    for (size_t offset = 0; (offset = value.find(key, offset)) != value.npos;
         offset += replacement.size())
      value.replace(offset, key.size(), replacement);
  return value;
}
}  // namespace

NativeInterface inspect_make(const ForeignRequest &request,
                             const Project &project,
                             const Toolchain &tools) {
  const auto &r = request.requirement;
  if (r.libraries.empty())
    throw std::runtime_error(
        "Makefile dependency '" + r.alias +
        "' requires libraries; Make has no public target metadata API");
  const auto makefile = request.source / "Makefile";
  if (!fs::is_regular_file(makefile))
    throw std::runtime_error("Makefile dependency has no Makefile: " + r.alias);
  auto make = find_program("gmake");
  if (make.empty())
    make = find_program("make");
  if (make.empty())
    make = find_program("mingw32-make");
  if (make.empty())
    throw std::runtime_error("Makefile dependencies require GNU Make");
  std::string selection = request.identity + ":make:" + r.target;
  for (const auto *const group : {&r.targets, &r.libraries})
    for (const auto &value : *group)
      selection += ':' + value;
  const auto id = source_key(selection);
  const auto build =
      project.build / layout::environment / "foreign" / id / "build";
  validate_generated_path(build);
  fs::create_directories(build);
  NativeInterface result;
  result.usage.id = id;
  result.usage.defines = r.defines;
  result.build = {id,
                  build,
                  {make,
                   "-f",
                   makefile.u8string(),
                   "-j",
                   "@CLUTCH_JOBS@",
                   "SOURCE_DIR=" + request.source.u8string(),
                   "BUILD_DIR=" + build.u8string(),
                   "VPATH=" + request.source.u8string(),
                   "CC=" + tools.cc,
                   "CXX=" + tools.cxx},
                  {}};
  if (!r.target.empty())
    result.build.arguments.push_back(r.target);
  result.build.arguments.insert(result.build.arguments.end(),
                                r.targets.begin(),
                                r.targets.end());
  for (const auto &argument : r.build_arguments)
    result.build.arguments.push_back(expand(argument, request.source, build));
  for (const auto &library : r.libraries) {
    const auto path =
        fs::absolute(build / fs::u8path(expand(library, request.source, build)))
            .lexically_normal();
    result.usage.link_options.push_back(path.u8string());
    result.usage.link_inputs.push_back(path);
    result.build.artifacts.push_back(path);
  }
  for (const auto &directory : r.include_dirs)
    result.usage.includes.push_back(
        fs::absolute(request.source /
                     fs::u8path(expand(directory, request.source, build)))
            .lexically_normal());
  for (const auto &runtime : r.runtime)
    result.usage.runtime_libraries.push_back(
        fs::absolute(build / fs::u8path(expand(runtime, request.source, build)))
            .lexically_normal());
  result.inputs.push_back({makefile, input_signature(makefile)});
  return result;
}
}  // namespace clutch::dependencies
