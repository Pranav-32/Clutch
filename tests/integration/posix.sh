#!/usr/bin/env bash
#
# Copyright (c) 2026, Pranav Sanjeevi Kumari
# SPDX-License-Identifier: BSD-3-Clause
#
# See LICENSE in the project root for the full license text.
#

# Exercise positional configuration and persisted builds on POSIX hosts using
# Ninja and GNU Make. Arguments: clutch, ninja, make, C compiler, C++ compiler.
# Covers C/C++, test targets, IDE metadata, rebuilds, profiles, scaffolding,
# application arguments, standard/warning flags and build-directory ownership.
# Fixtures remain under tests/work for diagnostics; no existing project is edited.
set -euo pipefail

if [[ $# -ne 5 ]]; then
    printf 'Usage: %s clutch ninja make cc cxx\n' "$0" >&2
    exit 2
fi

clutch="$1"
ninja="$2"
make_tool="$3"
cc="$4"
cxx="$5"
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$source_dir/tests/work"
work="$(mktemp -d "$source_dir/tests/work/posix.XXXXXX")"
log="$work/command.log"

# Preserve the last subprocess log and identify its retained fixture directory.
fail() {
    printf 'FAIL: %s\nFixtures retained in %s\n' "$1" "$work" >&2

    if [[ -f "$log" ]]; then
        cat "$log" >&2
    fi

    exit 1
}

# Preserve diagnostics and exit codes without piping commands through a logger.
invoke() {
    if ! "$@" >"$log" 2>&1; then
        fail "command failed: $*"
    fi
}

# Negative cases must return nonzero; callers also check the intended diagnostic.
expect_failure() {
    if "$@" >"$log" 2>&1; then
        fail "command unexpectedly succeeded: $*"
    fi
}

for generator in ninja make; do
    root="$work/$generator project with spaces"
    build="$root/output/debug with spaces"
    release="$root/output/release with spaces"
    tool="$ninja"

    if [[ "$generator" == make ]]; then
        tool="$make_tool"
    fi

    mkdir -p "$root/src" "$root/test"
    cat >"$root/clutch.toml" <<EOF
[project]
name = "hello"
version = "0.1.0"
language = "mixed"
std = { c = 11, cpp = 20 }

[build]
generator = "$generator"

[profile.debug]
build_dir = "output/debug with spaces"

[profile.release]
build_dir = "output/release with spaces"

[[test]]
name = "passing"
sources = ["test/pass.cpp"]

[[test]]
name = "failing"
sources = ["test/fail.c"]
EOF
    cat >"$root/src/value.h" <<'EOF'
#pragma once
#define VALUE 42
EOF
    cat >"$root/src/value.c" <<'EOF'
#include "value.h"
int value(void) { return VALUE; }
EOF
    cat >"$root/src/main.cpp" <<'EOF'
extern "C" int value(void);
int main() { return value() == 42 ? 0 : 1; }
EOF
    printf '%s\n' 'int main() { return 0; }' >"$root/test/pass.cpp"
    printf '%s\n' 'int main(void) { return 7; }' >"$root/test/fail.c"

    # Building must not implicitly configure a project or an unconfigured profile.
    expect_failure "$clutch" build --project "$root"
    grep -Fq -- 'not configured' "$log" || fail 'missing configuration diagnostic'
    invoke "$clutch" "$root" --build-tool "$tool" --cc "$cc" --cxx "$cxx"
    [[ -s "$build/configuration.toml" ]] || fail 'saved configuration missing'
    [[ -s "$build/build-graph.dot" ]] || fail 'build graph visualization missing'
    [[ -s "$build/compile_commands.json" ]] || fail 'build-local database missing'
    [[ ! -e "$root/compile_commands.json" ]] || fail 'database leaked into source root'
    [[ ! -e "$build/hello" ]] || fail 'configuration compiled application'

    if find "$build/obj" -type f -name '*.o' -print | grep -q .; then
        fail 'configuration compiled source units'
    fi

    # Generated database entries describe the configured build and both languages.
    grep -Fq -- '"directory": "'"$build"'"' "$build/compile_commands.json" \
        || fail 'database does not use the absolute build directory'
    grep -Fq -- '-std=c11' "$build/compile_commands.json" || fail 'C standard missing'
    grep -Fq -- '-std=c++20' "$build/compile_commands.json" || fail 'C++ standard missing'
    [[ $(grep -c '"file":' "$build/compile_commands.json") -eq 4 ]] \
        || fail 'database does not include every application/test source'

    # A filtered test builds only its selected target, not the application or
    # unrelated tests. All declared tests still exist in the saved graph.
    invoke "$clutch" test --project "$root" --filter passing
    grep -Fq -- '1 passed; 0 failed' "$log" || fail 'selected test did not run'
    [[ ! -e "$build/hello" ]] || fail 'selected test also built application'
    [[ ! -e "$build/tests/failing" ]] || fail 'selected test also built unrelated test'
    expect_failure "$clutch" test --project "$root"
    grep -Fq -- '1 passed; 1 failed' "$log" || fail 'test failure aggregation incorrect'

    invoke "$clutch" build --project "$root" --jobs 2
    invoke "$build/hello"
    stamp="$build/before-noop.stamp"
    touch "$stamp"
    sleep 1
    invoke "$clutch" build --project "$root"

    if find "$build/obj" -type f -name '*.o' -newer "$stamp" -print | grep -q .; then
        fail 'unchanged build recompiled a source'
    fi

    sleep 1
    printf '\n// changed header\n' >>"$root/src/value.h"
    invoke "$clutch" build --project "$root"
    grep -Fq -- 'Compiling' "$log" || fail 'header edit did not recompile'

    if ! find "$build/obj" -type f -name '*.o' -newer "$stamp" -print | grep -q .; then
        fail 'header edit did not update an object'
    fi

    # Release is independently configured; changed manifest content invalidates
    # saved debug state even if only a comment changed.
    expect_failure "$clutch" build --project "$root" --release
    grep -Fq -- 'not configured' "$log" || fail 'missing release configuration diagnostic'
    invoke "$clutch" "$root" --release --build-tool "$tool" --cc "$cc" --cxx "$cxx"
    invoke "$clutch" build --project "$root" --release
    [[ -s "$release/configuration.toml" ]] || fail 'release configuration missing'
    [[ -s "$release/compile_commands.json" ]] || fail 'release database missing'
    invoke "$release/hello"
    printf '\n# changed manifest\n' >>"$root/clutch.toml"
    expect_failure "$clutch" build --project "$root"
    grep -Fq -- 'changed since configuration' "$log" || fail 'stale configuration accepted'
    invoke "$clutch" "$root" --build-tool "$tool" --cc "$cc" --cxx "$cxx"
    invoke "$clutch" build --project "$root"

    printf 'PASS: %s configuration, mixed C/C++, tests, incremental builds and profile state\n' \
        "$generator"
done

# Exercise developer commands as well as generated builds. Preserve existing
# files during init, forward application arguments and keep profiles independent.
workflow="$work/developer workflow"
invoke "$clutch" new "$workflow" --name workflow
grep -Fq -- '[project]' "$workflow/clutch.toml" || fail 'new used an outdated manifest table'
expect_failure "$clutch" init "$workflow"
cat >"$workflow/src/main.cpp" <<'EOF'
#include <cstring>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3 || std::strcmp(argv[1], "two words") || std::strcmp(argv[2], "--flag")) return 23;
    std::cout << "arguments preserved\n";
}
EOF
invoke "$clutch" "$workflow" --build-tool "$ninja" --cc "$cc" --cxx "$cxx"
invoke "$clutch" run --project "$workflow" -- 'two words' --flag
grep -Fq -- 'arguments preserved' "$log" || fail 'run lost application arguments'
expect_failure "$clutch" run --project "$workflow" -- wrong
grep -Fq -- 'Running workflow' "$log" || fail 'argument failure did not come from the application'
invoke "$clutch" "$workflow" --release --build-tool "$ninja" --cc "$cc" --cxx "$cxx"
invoke "$clutch" build --project "$workflow" --release
invoke "$clutch" clean --project "$workflow"
[[ ! -e "$workflow/Clutch-build-debug" ]] || fail 'clean left the selected profile'
[[ -s "$workflow/Clutch-build-release/workflow" ]] || fail 'clean removed the other profile'
[[ -s "$workflow/src/main.cpp" ]] || fail 'clean removed application sources'

existing="$work/existing C project"
mkdir -p "$existing/src"
printf '%s\n' 'int main(void) { return 0; }' >"$existing/src/main.c"
invoke "$clutch" init "$existing" --name existing_c --language c
grep -Fq -- 'int main(void) { return 0; }' "$existing/src/main.c" || fail 'init changed existing sources'
invoke "$clutch" "$existing" --build-tool "$ninja" --cc "$cc" --cxx "$cxx"
invoke "$clutch" run --project "$existing"
printf 'PASS: new/init, arguments and exit failures, independent profiles and safe clean\n'

# Real C++17 features and a default-enabled deprecation warning catch a wrong
# standard or an implementation that only omits -Wall for warnings = "none".
for generator in ninja make; do
    policy="$work/$generator language and warning policy"
    tool="$ninja"

    if [[ "$generator" == make ]]; then
        tool="$make_tool"
    fi

    mkdir -p "$policy/src"
    cat >"$policy/src/main.cpp" <<'EOF'
#include <optional>
static_assert(__cplusplus == 201703L, "Clutch must select C++17");
[[deprecated("CLUTCH_PROFILE_WARNING")]] int legacy() { return 42; }
int main() {
    const std::optional value{legacy()};
    return *value == 42 ? 0 : 1;
}
EOF

    for warnings in default none extra; do
        cat >"$policy/clutch.toml" <<EOF
[project]
name = "policy"
version = "0.1.0"
std = 17

[build]
generator = "$generator"

[profile.debug]
warnings = "$warnings"
EOF
        invoke "$clutch" "$policy" --build-tool "$tool" --cc "$cc" --cxx "$cxx"
        database="$policy/Clutch-build-debug/compile_commands.json"
        grep -Fq -- '-std=c++17' "$database" || fail 'policy database omitted C++17'
        invoke "$clutch" build --project "$policy"

        if [[ "$warnings" == none ]]; then
            grep -Fq -- '"-w"' "$database" || fail 'policy database omitted warning suppression'

            if grep -Fq -- 'CLUTCH_PROFILE_WARNING' "$log"; then
                fail 'warnings = none still emitted the compiler warning'
            fi
        else
            grep -Fq -- 'CLUTCH_PROFILE_WARNING' "$log" || fail 'default/extra warnings were suppressed'
        fi

        invoke "$clutch" run --project "$policy"
    done

    printf 'PASS: %s C++17 compiler/database settings and default/none/extra warnings\n' "$generator"
done

# Migration and ownership errors must stay readable and preserve unrelated data.
invalid="$work/manifest validation"
mkdir -p "$invalid/src" "$invalid/user-data"
printf '%s\n' 'int main() { return 0; }' >"$invalid/src/main.cpp"
printf '%s\n' 'preserve' >"$invalid/user-data/keep.txt"
cat >"$invalid/clutch.toml" <<'EOF'
[package]
name = "legacy"
version = "0.1.0"
EOF
expect_failure "$clutch" "$invalid" --build-tool "$ninja" --cc "$cc" --cxx "$cxx"
grep -Fq -- 'rename [package] to [project]' "$log" || fail 'legacy table migration diagnostic missing'
cat >"$invalid/clutch.toml" <<'EOF'
[project]
name = "validation"
version = "0.1.0"
std = 17
[profile.debug]
build_dir = "user-data"
EOF
expect_failure "$clutch" "$invalid" --build-tool "$ninja" --cc "$cc" --cxx "$cxx"
grep -Fq -- 'unowned nonempty build directory' "$log" || fail 'unowned build directory was accepted'
expect_failure "$clutch" clean --project "$invalid"
grep -Fq -- 'preserve' "$invalid/user-data/keep.txt" || fail 'validation/clean removed unrelated data'
printf 'PASS: manifest migration and unowned-directory protection\n'

printf 'All POSIX integration checks passed. Fixtures retained in %s\n' "$work"
