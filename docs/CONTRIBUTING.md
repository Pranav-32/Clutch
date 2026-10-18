# Contributing to Clutch

Workspace/lock changes must update the [manifest reference](manifest.md),
[workspace guide](workspaces.md), [lock contract](lock-files.md), file-API schemas
and affected examples/tests. Run `clutch.workspace` with the existing suites.
Keep portable `clutch.lock` files with example manifests; keep machine-local
`backend.toml`, `toolchain.toml` and generated build files out of source control.
Use `--locked` when verifying an existing resolution, and regenerate normally
when deliberately changing project versions, members or cross-project links.

Clutch is a native C++17 build tool for C and C++ projects. Develop Clutch using
its CMake project, keep each change focused and include checks that demonstrate
the resulting behavior.

## Choose your workspace

The [development-container guide](development-container.md) describes a shared
Ubuntu development environment for Linux, macOS and Windows hosts. Docker and
the devcontainer definition give each checkout the same compiler/build/test
setup across machines. The image includes CMake, Ninja, GNU Make, GCC, Clang,
Graphviz and LLVM 22's formatting and analysis tools. Open it with CLion,
VS Code or Docker directly.

For native development, install CMake 3.20 or newer and a C++17 compiler. Install
the build tools needed for the generators you want to exercise:

| Host | Native development tools |
| --- | --- |
| Linux | GCC/G++ or Clang, plus Ninja or GNU Make |
| macOS | Apple Clang and Ninja or Make; a full Xcode installation for the Xcode generator |
| Windows | Visual Studio C++ Build Tools or Visual Studio with the Desktop development with C++ workload; Ninja, MSBuild, NMake or JOM as needed |

Run MSVC-based commands from a Visual Studio developer terminal so its compiler,
linker and SDK are available. Keep separate build directories for each host,
compiler and generator, including the container's `build-container` directory.

## Build and check Clutch

Run commands in this guide from the checkout root. See the
[testing guide](testing.md) for runner settings, Windows scripts, coverage and
test-workspace cleanup.

On Linux or macOS, the complete build-and-test workflow is:

```bash
bash tests/scripts/verify.sh
CC=clang CXX=clang++ CLUTCH_BUILD_JOBS=4 CLUTCH_TEST_BUILD_DIR=build-native-clang bash tests/scripts/verify.sh
```

The runner requires Ninja and GNU Make and verifies that CTest registered the
POSIX integration suite. It builds Clutch and exercises both generated backends.
The devcontainer initialization delegates to this runner using `build-container`.

From the checkout root, a native Ninja development build uses:

```text
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-native --parallel 2
ctest --test-dir build-native --output-on-failure
```

For a Visual Studio CMake build, select your installed Visual Studio generator
with `-G` using its name from `cmake --help`. Configure a separate directory such
as `build-native-vs`, then select the configuration when building and testing:

```text
cmake --build build-native-vs --config Debug --parallel 2
ctest --test-dir build-native-vs -C Debug --output-on-failure
```

In the development container, `bash .devcontainer/post-create.sh` configures,
builds and checks Clutch in `build-container`.

## Code organization and style

- Keep module headers beside their implementations under the appropriate `src/`
  subdirectory. Keep upstream headers and licenses under `third_party/`.
- Put native unit checks in `tests/unit/`, compiler-backed workflows in
  `tests/integration/`, and CI/release unit checks in `tests/tooling/`. Register
  native suites in `tests/CMakeLists.txt`; contributor runners and cleanup belong
  in `tests/scripts/`. Keep generated fixtures under ignored `tests/work/`.
- Make clock conditions explicit in incremental-build tests. Native tools compare
  timestamps, while Clutch validates saved input contents. Establish the required
  timestamp order or simulate a coarse clock rather than relying on runner speed.
- Follow [`.clang-format`](../.clang-format) and [`.clang-tidy`](../.clang-tidy).
  Use `const` or `constexpr` where appropriate and structured bindings when
  decomposing records.
- Separate independent conditionals and meaningful code blocks with blank lines.
- Prefer C++17 `if` initializers for branch-scoped values. Use unsigned operands
  and literals for bitwise operations, such as `byte >> 4U` and `byte & 15U`.
  Give algorithm and format constants names and comments explaining their roles;
  use `ULL` or fixed-width constant macros for 64-bit hash constants.
- Avoid redundant type qualifiers and explicit template arguments when type
  deduction is clear. Preserve meaningful `const` and reference qualifiers.
- Document source files, function contracts and setup scripts. Explain each
  meaningful processing stage, ownership/lifetime assumptions and non-obvious
  platform behavior inline. Keep comments about intent and invariants rather
  than restating individual expressions, and update them with the implementation.
- Update the README and relevant guides when features or workflows change.
- Keep host paths and tool selections configurable or discoverable. Share policy
  constants and explain format/algorithm values instead of repeating magic numbers.
  Protocol tokens and intentional fixture inputs may remain literal; tests should
  discover configured paths/artifacts instead of assuming platform file names.
- Use a bare `@file` in file-local Doxygen blocks. Doxygen associates the block
  with its containing file without an IDE-resolved path.
  Put `/**`, each Doxygen command such as `@file` and `@brief`, and the closing
  `*/` on separate lines; prefix comment content with ` * `.
- Keep the target graph independent of individual generators. Preserve source
  language, per-source flags, dependency tracking and profile settings.

Use LLVM 22's formatter so the checked-in configuration is understood. In a
Linux, macOS or container shell, check project-owned sources with:

```bash
find src examples -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 \
    | xargs -0 clang-format --dry-run --Werror
```

In PowerShell:

```powershell
Get-ChildItem src, examples -Recurse -File |
    Where-Object { $_.Extension -in '.c', '.cpp', '.h', '.hpp' } |
    ForEach-Object { clang-format --dry-run --Werror $_.FullName }
```

To apply formatting, replace `--dry-run --Werror` with `-i` in the matching
command above. Format only project-owned code. See the
[static-analysis guide](static-analysis.md) for clang-tidy usage and the
[architecture guide](architecture.md) for module responsibilities.

For maintainer copyright headers, use the
[CLion copyright profile and automation settings](clion-copyright.md). The
template includes the `BSD-3-Clause` SPDX identifier and applies only to owned
sources. Preserve upstream notices and other contributors' copyrights.

## Validate generator behavior

Run the relevant CMake build and CTest checks. For generator changes, exercise
configuration, compilation, linking, an unchanged rebuild and a header edit using
the actual backend. Cover C, C++ and mixed-language projects, and paths containing
spaces. Clutch configuration uses `clutch .`; a project's generator is selected
through `[build].generator` or the configuration command's `--generator` option.

The Linux container provides a common environment for portable code, Ninja and
GNU Make. Visual Studio/MSBuild, NMake and JOM require Windows validation; Xcode
requires macOS and Apple's tools. State which hosts and generators you actually
tested, including checks that remain unavailable in your environment.

Inspect `build-graph.dot` in a configured Clutch project's profile directory with
Graphviz when changing graph construction or generators. The container guide
includes rendering commands. `clutch.graph` tests cycle and duplicate-output
rejection, DOT escaping, and rendering when Graphviz is available.

## Submit a change

Use Conventional Commit titles such as `fix: ...` or `feat: ...` so automated
version proposals can classify changes. See [versions and releases](releases.md)
for the version policy, CI matrix, tag workflow and release PR setup.

Contributions to Clutch's own code and documentation are accepted under
[BSD 3-Clause](../LICENSE). Submit only material you have the right to contribute,
and preserve existing copyright and third-party notices. See the
[licensing guide](licensing.md) for contribution and redistribution rules.

Create a branch in your fork and keep generated build trees, local IDE settings
and temporary fixtures out of the change. Describe the problem and resulting
behavior in your pull request, list the checks you ran and identify material
limitations. Include a small reproduction for a bug. Keep unrelated formatting
changes separate so reviewers can assess the implementation.
