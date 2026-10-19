# Contributing to Clutch

Develop Clutch with its CMake project. Keep changes focused, demonstrate the
resulting behavior with appropriate checks, and update affected documentation
and examples.

## Build and test

Use the [development container](development-container.md), or install CMake
3.20+, a C++17 compiler, Ninja and the native tools for the generators being
tested. Python is needed for development tests, not for the Clutch executable.
MSVC commands require a Visual Studio developer terminal.

From the checkout root:

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-native --parallel 2
ctest --test-dir build-native --output-on-failure
```

On Windows, also pass `-DCLUTCH_TEST_NINJA="path/to/ninja.exe"` during
configuration to enable compiler-backed suites. Multi-configuration generators
need `--config Debug` for building and `-C Debug` for CTest.

See [testing](testing.md) for complete platform runners, prerequisites,
suite selection and cleanup. Keep separate build directories for each host,
compiler and generator. Open a draft PR for CI feedback; new commits update
the existing PR checks.

## Code organization and style

- Project artwork lives in [`assets/branding/`](../assets/branding/).
  Keep the SVG logo as the vector source, its transparent PNG as the raster
  export, and the 1280 × 640 banner for the README and GitHub social preview.
  Preserve the supplied artwork when changing documentation.
- Keep module headers beside their implementations under `src/`. Vendored code
  and upstream notices belong in `third_party/`.
- Follow [`.clang-format`](../.clang-format) and [`.clang-tidy`](../.clang-tidy).
  Use LLVM 22's tools for the checked-in configuration.
- Keep CMake files consistent with their four-space indentation and separate
  meaningful setup blocks with blank lines.
- Use `const`/`constexpr` where appropriate, structured bindings for records,
  and C++17 `if` initializers for branch-scoped values.
- Avoid redundant type qualifiers and template arguments where deduction is
  clear. Preserve meaningful constness and reference semantics.
- Separate independent conditionals and meaningful blocks with blank lines.
- Use unsigned operands/literals in bitwise operations, such as `byte >> 4U`.
  Name format/algorithm constants and explain their purpose. Keep host paths
  and tool selections configurable or discoverable.
- Document file responsibilities, function contracts, lifetime/ownership rules
  and non-obvious platform behavior. Explain intent instead of repeating code.
  Put each Doxygen command on its own line, using a bare `@file`.
- Keep manifest resolution and the target graph independent of generators.
  Derive native rules, compiler databases and file-API data consistently.
- Keep paths as filesystem values internally and use UTF-8 at text boundaries:
  decode with `fs::u8path` and serialize with `u8string`/`generic_u8string`.
  Read environment values with `platform/environment.hpp` so Windows paths
  are not converted through the ANSI code page.
- Update relevant reference guides and schemas when public behavior changes.
  Keep the README focused on installation and use.

Check formatting of owned sources, excluding generated or vendored files:

```sh
git ls-files -z -- 'src/*.cpp' 'src/*.hpp' 'tests/unit/*.cpp' 'examples/*.cpp' 'examples/*.hpp' 'examples/*.c' 'examples/*.h' \
  | xargs -0 clang-format --dry-run --Werror
```

In PowerShell:

```powershell
git ls-files -- 'src/*.cpp' 'src/*.hpp' 'tests/unit/*.cpp' 'examples/*.cpp' 'examples/*.hpp' 'examples/*.c' 'examples/*.h' |
    ForEach-Object { clang-format --dry-run --Werror $_ }
```

Replace `--dry-run --Werror` with `-i` to apply formatting. Keep unrelated
formatting changes out of functional patches.

## Static analysis

For a compilation database with the actual compiler settings:

```sh
cmake -S . -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-tidy --parallel 2
clang-tidy --config-file=.clang-tidy --verify-config
clang-tidy -p build-tidy src/main.cpp
```

Select the desired compiler on initial configuration. Keep the database in its
build directory. The advisory baseline covers analyzer, bug-prone, performance,
portability and selected modernization/readability checks. Analyze owned sources
only, review findings before applying fixes, and justify narrowly scoped
`NOLINT` suppressions. Passing build tests does not establish clean analysis.

## Tests and behavior changes

Put native graph checks in `tests/unit/`, compiler-backed workflows in
`tests/integration/`, and CI/release tests in `tests/tooling/`. Register native
suites in `tests/CMakeLists.txt`; runners and cleanup belong in `tests/scripts/`.
Keep generated fixtures in ignored build directories or `tests/work/`.

For generator changes, exercise real configuration, compilation, linking, an
unchanged rebuild and a header/flag change. Cover C/C++, mixed sources and paths
with spaces. Establish timestamp ordering explicitly in incremental tests or
simulate a coarse clock; do not rely on runner speed. Discover configured
artifacts instead of assuming one platform's names.

Workspace/lock changes need corresponding manifest, workspace, lock and file-API
updates where applicable. Commit portable example locks with their manifests;
exclude host-specific saved configurations and generated build files. Use
`--locked` to verify existing resolution and regenerate normally after deliberate
membership/version/link changes.

## Submit a change

Report suspected vulnerabilities through the private channel in the
[security policy](../.github/SECURITY.md). Keep sensitive reproductions out of
public issues and pull requests.

GitHub-managed CodeQL default setup scans C/C++, Python and workflow actions.
Dependabot proposes grouped weekly updates to pinned GitHub Actions. Vendored
C++ headers still need upstream advisory review when updating them; see the
[dependency notes](../third_party/README.md).

Create a branch in your fork and open a pull request. Describe the problem and
resulting behavior, include a small reproduction for bugs, and list checks run
with any host limitations. Windows generators require Windows; Xcode requires
macOS. The Linux container cannot validate those generators.

Use Conventional Commit PR titles such as `fix: ...` or `feat: ...` for automated
version proposals. Contributors do not tag releases or update `VERSION` for
ordinary changes. See [versions and CI](releases.md).

Contributions use [BSD 3-Clause](../LICENSE). Preserve copyright and upstream
notices; see [licensing](licensing.md). An optional
[CLion copyright template](clion-copyright.md) is provided. Keep local IDE
settings, credentials and generated artifacts out of the PR.
