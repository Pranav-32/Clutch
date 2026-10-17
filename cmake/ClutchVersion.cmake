# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Resolve release identity without requiring Git or a network connection.
include_guard(GLOBAL)

# Read the next/current release version. Keep numeric core and prerelease
# canonical so tags, package names and CMake's project version agree.
function(clutch_read_version source_dir output)
  file(READ "${source_dir}/VERSION" version)
  string(STRIP "${version}" version)
  set(number "(0|[1-9][0-9]*)")
  set(identifier "(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)")
  if(NOT version MATCHES "^${number}\\.${number}\\.${number}(-${identifier}(\\.${identifier})*)?$")
    message(FATAL_ERROR "VERSION must contain a canonical major.minor.patch[-prerelease] version")
  endif()
  set(${output} "${version}" PARENT_SCOPE)
endfunction()

# Run Git in this source root only. Failures are data, not fatal configuration
# errors: downloaded source archives and machines without Git remain buildable.
function(clutch_git source_dir output status)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" ${ARGN}
    OUTPUT_VARIABLE result RESULT_VARIABLE exit_code
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET TIMEOUT 10)
  set(${output} "${result}" PARENT_SCOPE)
  set(${status} "${exit_code}" PARENT_SCOPE)
endfunction()

# Reevaluate on every build. configure_file writes only changed contents, so a
# no-op build does not recompile the CLI while commits and dirty state refresh.
function(clutch_generate_version source_dir binary_dir)
  clutch_read_version("${source_dir}" base_version)
  set(CLUTCH_VERSION "${base_version}+source")
  find_package(Git QUIET)
  if(GIT_FOUND)
    clutch_git("${source_dir}" git_root result rev-parse --show-toplevel)
    file(REAL_PATH "${source_dir}" physical_source)
    if(result EQUAL 0)
      file(REAL_PATH "${git_root}" physical_git_root)
    endif()

    # Do not accidentally identify an extracted source tree with its parent's Git
    # repository. Worktrees are supported because Git resolves their metadata.
    if(result EQUAL 0 AND physical_source STREQUAL physical_git_root)
      # Twelve hex digits provide a useful, compact commit identifier. Git may
      # extend this abbreviation if more digits are required for uniqueness.
      clutch_git("${source_dir}" commit commit_result rev-parse --short=12 HEAD)
      clutch_git("${source_dir}" changes status_result status --porcelain --untracked-files=normal)
      if(commit_result EQUAL 0 AND status_result EQUAL 0 AND commit MATCHES "^[0-9a-f]+$")
        clutch_git("${source_dir}" tag tag_result describe --tags --exact-match --match "v${base_version}" HEAD)
        if(tag_result EQUAL 0 AND tag STREQUAL "v${base_version}" AND changes STREQUAL "")
          set(CLUTCH_VERSION "${base_version}")
        else()
          if(base_version MATCHES "-")
            set(CLUTCH_VERSION "${base_version}.dev+g${commit}")
          else()
            set(CLUTCH_VERSION "${base_version}-dev+g${commit}")
          endif()

          if(NOT changes STREQUAL "")
            string(APPEND CLUTCH_VERSION ".dirty")
          endif()

          clutch_git("${source_dir}" shallow shallow_result rev-parse --is-shallow-repository)
          if(shallow_result EQUAL 0 AND shallow STREQUAL "true")
            string(APPEND CLUTCH_VERSION ".shallow")
          endif()
        endif()
      endif()
    endif()
  endif()

  file(MAKE_DIRECTORY "${binary_dir}/generated/core")
  configure_file("${source_dir}/src/core/version.hpp.in"
    "${binary_dir}/generated/core/version.hpp" @ONLY)
  # Packaging reads the same identity compiled into --version.
  file(CONFIGURE OUTPUT "${binary_dir}/generated/version.txt" CONTENT "${CLUTCH_VERSION}\n" @ONLY)
endfunction()
