#!/usr/bin/env bash
#
# Copyright (c) 2026, Pranav Sanjeevi Kumari
# SPDX-License-Identifier: BSD-3-Clause
#
# See LICENSE in the project root for the full license text.
#

# Configure, build and test Clutch on Linux or macOS, including containers.
# Usage: bash tests/verify.sh
# CC/CXX select compiler executables. CLUTCH_BUILD_JOBS defaults to 2;
# CLUTCH_TEST_BUILD_DIR defaults to build-native relative to the repository;
# CLUTCH_TEST_EXPORT_DATABASE defaults to OFF (the devcontainer sets ON).
# Requires CMake/CTest, Bash, Ninja, GNU Make and C/C++ compilers. The POSIX
# integration test must be registered; missing tools cannot silently skip it.
# Uses Bash 3.2-compatible syntax for macOS's system Bash.
set -euo pipefail

# Stop with a user-facing diagnostic; native tool failures retain their exit code.
fail() {
    printf 'Verification failed: %s\n' "$1" >&2
    exit 1
}

# Require a native POSIX host; Git Bash cannot validate Linux/macOS subprocesses.
case "$(uname -s)" in
    Linux|Darwin) ;;
    *) fail 'run this script on Linux or macOS; use tests/windows-verify.cmd on Windows' ;;
esac

if [[ $# -ne 0 ]]; then
    fail 'usage: bash tests/verify.sh (configure CC, CXX and CLUTCH_BUILD_JOBS through the environment)'
fi

source_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
# Anchor relative output paths to the checkout, even when launched elsewhere.
build_dir="${CLUTCH_TEST_BUILD_DIR:-build-native}"
case "$build_dir" in
    /*) ;;
    *) build_dir="$source_dir/$build_dir" ;;
esac
jobs="${CLUTCH_BUILD_JOBS:-2}"
export_database="${CLUTCH_TEST_EXPORT_DATABASE:-OFF}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'CLUTCH_BUILD_JOBS must be a positive integer'
[[ "$export_database" == ON || "$export_database" == OFF ]] \
    || fail 'CLUTCH_TEST_EXPORT_DATABASE must be ON or OFF'

# Resolve the first usable executable to an absolute path for CMake's cache.
# Each argument is a candidate name/path, never a shell command with extra flags.
require_tool() {
    local name resolved
    for name in "$@"; do
        if resolved="$(command -v "$name")" && [[ -f "$resolved" && -x "$resolved" ]]; then
            printf '%s/%s\n' \
                "$(CDPATH= cd -- "$(dirname -- "$resolved")" && pwd -P)" \
                "$(basename -- "$resolved")"
            return 0
        fi
    done

    fail "required executable not found: $*"
}

# Resolve all prerequisites before creating a build tree or starting compilation.
cmake="$(require_tool cmake)"
ctest="$(require_tool ctest)"
bash_tool="$(require_tool bash)"
ninja="$(require_tool ninja ninja-build)"
make_tool="$(require_tool gmake make)"
cc="$(require_tool "${CC:-cc}")"
cxx="$(require_tool "${CXX:-c++}")"
make_version="$("$make_tool" --version)"
[[ "$make_version" == *'GNU Make'* ]] || fail 'the integration suite requires GNU Make (make or gmake)'

# Pass exact tool paths so CMake registers tests with the requested compilers.
"$cmake" -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_MAKE_PROGRAM="$ninja" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS="$export_database" \
    -DBUILD_TESTING=ON \
    -DCLUTCH_TEST_BASH="$bash_tool" \
    -DCLUTCH_TEST_POSIX_NINJA="$ninja" \
    -DCLUTCH_TEST_MAKE="$make_tool" \
    -DCLUTCH_TEST_CC="$cc"

"$cmake" --build "$build_dir" --parallel "$jobs"
# CTest can succeed with only smoke tests if optional tools were missing at
# configuration time. Require the compiler-backed suite before claiming success.
registered_tests="$("$ctest" --test-dir "$build_dir" -N)"
[[ "$registered_tests" == *'clutch.posix-integration'* ]] \
    || fail 'clutch.posix-integration was not registered by CMake'

"$ctest" --test-dir "$build_dir" --output-on-failure
printf '\nClutch verification passed: %s/clutch\n' "$build_dir"
