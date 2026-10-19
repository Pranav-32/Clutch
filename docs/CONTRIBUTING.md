# Contributing to Clutch

Audience: contributors. Start here; reference guides are available when needed.

The [README](../README.md) introduces Clutch and its basic usage. This guide is
the entry point for contributing: setup, where to edit, conventions, checks and
review. The related guides below supply details for particular kinds of changes.

Documentation fixes, examples, tests and bug reports are welcome alongside C/C++
changes. You do not need to understand every backend to make a useful contribution.
Please follow the [code of conduct](../.github/CODE_OF_CONDUCT.md).

## Your first pull request

Clutch reads project manifests and generates native C/C++ build files. CMake
builds Clutch itself. You can contribute without learning every generator or
dependency adapter first.

1. Choose one small change and create a branch in your fork. Browse
   [good first issues](https://github.com/Pranav-32/Clutch/labels/good%20first%20issue),
   or ask on an issue before starting larger work.
2. Build Clutch with the [development container](development-container.md) or the
   [native commands below](#build-and-test). A prose-only correction can skip
   compiler setup: edit, preview and check the affected links instead.
3. For code or example changes, run one small project before editing. From the
   checkout root after a Ninja build:

   ```sh
   ./build-native/clutch examples/single-file
   ./build-native/clutch run --project examples/single-file
   ```

   In the development container, use `./build-container/clutch` instead.
   On Windows, use `.\build-native\clutch.exe` in an initialized developer
   terminal. A multi-configuration Debug build places it under `build-native/Debug/`.
   The example should print `Hello from Clutch and C++!`.
4. Edit the relevant files and run the checks in the table below. Format changed
   C/C++ files. Record commands that passed and checks you could not run, with
   the missing tool or host.
5. Open a focused PR with a Conventional Commit title such as
   `docs: explain dependency paths` or `fix: preserve target include order`.
   Explain the problem and resulting behavior; link an issue when relevant.
   Draft PRs are welcome.
6. Respond to review on the same branch. GitHub runs the
   [platform checks](releases.md#continuous-verification). You do not need every
   operating system locally or a release tag for an ordinary contribution.

### Where to edit and what to check

| Change | Files to start with | Smallest useful check |
| --- | --- | --- |
| Documentation | `README.md` or the relevant `docs/` guide | Preview formatting; verify changed file links and heading anchors |
| Example | Its directory under `examples/` | Configure it, then run it and any declared tests; follow its README |
| Native behavior | The module and paired header under `src/`; see [code layout](architecture.md#code-layout) | Rebuild Clutch, run the relevant [CTest suites](testing.md#select-checks), then format/analyze changed C/C++ |
| Test or automation | `tests/`, or the script being changed | Run the affected suite; use [tooling tests](testing.md#select-checks) for Python automation |

For a first documentation PR, clarify one confusing explanation, verify its
links and commands, and avoid unrelated formatting. Use
[Get help](#get-help-and-work-with-reviewers) if setup fails.

The remaining sections are references. Read the ones that match your change:
[build commands](#build-and-test), [style](#code-organization-and-style),
[static analysis](#static-analysis), [test design](#tests-and-behavior-changes)
or [submission](#submit-a-change). Release operations belong in the
[maintainer guide](maintaining.md).

### Related contributor references

| Your change involves… | Read as needed |
| --- | --- |
| Configuration flow or module boundaries | [Architecture](architecture.md) |
| Manifest fields or dependency adapters | [Manifest rules](manifest.md) and [dependency support](dependencies.md#contributing-to-dependency-support) |
| Workspace composition or saved resolution | [Workspaces](workspaces.md) and [lock files](lock-files.md) |
| IDE metadata or protocol compatibility | [File API](file-api.md) |
| Test coverage or platform setup | [Testing](testing.md) and [development container](development-container.md) |
| Repository automation or publication | [Maintainer guide](maintaining.md) |

## Get help and work with reviewers

Use [GitHub issues](https://github.com/Pranav-32/Clutch/issues/new/choose) for setup
questions, bugs and feature proposals. Search existing issues first. Include your
Clutch version, platform, compiler/generator and a small manifest or command when
relevant. Say what you tried; it is fine to ask before you have a proposed fix.
Security reports use the [private reporting channel](../.github/SECURITY.md).

Maintainers triage reports, request missing reproductions and identify scoped
`good first issue` or `help wanted` work. Review focuses on behavior, compatibility,
clarity and relevant checks. Explain disagreements with examples and evidence;
address the code rather than the person. Review is volunteer work with no promised
response deadline. A passing CI run does not guarantee acceptance of a proposal.

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
python tests/scripts/tidy.py --database build-tidy --scope src --scope tests/unit
```

Select the desired compiler on initial configuration. Keep the database in its
build directory. The advisory baseline covers analyzer, bug-prone, performance,
portability and selected modernization/readability checks. Analyze owned sources
only, review findings before applying fixes, and justify narrowly scoped
`NOLINT` suppressions. Passing build tests does not establish clean analysis.

The analysis runner fails if any source in its selected scope lacks a compilation
command, validates the clang-tidy configuration, and treats enabled diagnostics
as errors. Omit `--scope` to cover `src/`, `tests/unit/` and all examples; supply
every configured example database with another `--database` argument. To extend
the command above with just the flexible example, configure it and add both
`--scope examples/flexible` and `--database examples/flexible/Clutch-build-debug`. Use
`--jobs 2` for limited parallel analysis. Each database must describe the current
host; headers are analyzed through the units that include them, and inactive
platform branches need checks on their own host.

LLVM 22's const-correctness check does not catch every deduced `auto` pointer or
parameter binding. Review these explicitly: `const auto *const node` makes both
the pointee and binding immutable; `auto *const node` keeps the pointee mutable.
Qualify unchanged parameter bindings in definitions where useful, keeping
redundant top-level `const` out of declarations. A clean analysis run does not
replace that review.

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

Changes to scanning, dependency-update or release automation follow the
[maintainer guide](maintaining.md#security-maintenance).

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
