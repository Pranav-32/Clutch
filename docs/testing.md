# Testing Clutch

Audience: Contributors choosing and running checks for their changes.

Run commands from the checkout root. These checks develop Clutch itself;
tests in projects built by Clutch use the [project workflow](usage.md#project-workflows).

| Directory | Purpose |
| --- | --- |
| `tests/unit/` | Native graph and safety checks |
| `tests/integration/` | Compiler-backed workflows, generators, file API and workspaces |
| `tests/tooling/` | CI, release packaging and Git versioning |
| `tests/scripts/` | Verification runners and fixture cleanup |
| `tests/work/` | Ignored generated fixtures |

Windows target and dependency fixtures use `tests/work/` because MSBuild excludes
the system TEMP tree from header read tracking. POSIX fixtures use local temporary
storage to avoid bind-mount overhead. Each suite removes its own fixture directories.

## Linux, macOS and containers

Install CMake/CTest, Bash, Python, Ninja, GNU Make, Git and C/C++ compilers. Python
3.12+ supports the complete development/CI tooling; native integration tests
require at least 3.9. Graphviz enables the optional DOT rendering check. A full
Xcode installation is needed to test the Xcode generator.

```sh
bash tests/scripts/verify.sh
```

The runner configures a Ninja build and requires the compiler-backed suites to
be registered, then runs CTest. Adjust its environment as needed:

```sh
CC=clang CXX=clang++ CLUTCH_BUILD_JOBS=4 \
  CLUTCH_TEST_BUILD_DIR=build-native-clang bash tests/scripts/verify.sh
```

| Setting | Default |
| --- | --- |
| `CC` / `CXX` | `cc` / `c++` |
| `CLUTCH_BUILD_JOBS` | `2` |
| `CLUTCH_TEST_BUILD_DIR` | `build-native`, relative to the checkout |
| `CLUTCH_TEST_EXPORT_DATABASE` | `OFF`; `ON` exports CMake's database |

The [container](development-container.md) uses this runner with Clang,
`build-container` and database export enabled.

## Windows

Use a Visual Studio developer terminal with CMake, Ninja and Python installed:

```powershell
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCLUTCH_TEST_NINJA="path/to/ninja.exe"
cmake --build build-native --parallel 2
ctest --test-dir build-native --output-on-failure
```

Replace the Ninja path with its installed location. For a Visual Studio CMake
generator, build with `--config Debug` and test with `-C Debug`.
`tests/scripts/windows-verify.cmd <vcvars64.bat> <ninja.exe> [clutch.exe]` also
runs MSVC/Clang integration checks with an initialized environment.

## Select checks

List registered tests before relying on a run; missing tools can omit optional
suites. CI and the POSIX verification runner require the relevant integration
registrations.

```sh
ctest --test-dir build-native -N
ctest --test-dir build-native -R 'targets|workspace|file-api' --output-on-failure
python -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

| Suite | Coverage |
| --- | --- |
| `clutch.help`, `clutch.version` | CLI startup and version identity |
| `clutch.standalone` | Windows MSVC executable startup without compiler DLL paths |
| `clutch.graph` | Graph validation, deterministic DOT escaping and optional rendering |
| `clutch.files` | Atomic directory publication, existing-output preservation and Windows sharing-handle retries |
| `clutch.zip` | Archive path traversal, Windows device names, invalid UTF-8, case/hierarchy collisions, member bounds and existing-output preservation |
| `clutch.bundle` | Independent ZIP/CRC parsing, repeatable bytes, Unicode/binary files, exclusions, workspace relocation and offline Git/local/CMake dependency snapshots; includes 256 files and a one-MiB binary fixture |
| `clutch.source-files` | Recursive source matching, brace products, canonical paths, exclusions and invalid patterns without a compiler |
| `clutch.capture` | Native argv boundaries, exit status, exact four-MiB output boundary and oversized-output rejection |
| `clutch.stress-source-files` | 2,048 source files with 64 alternatives/repeated patterns; expansion bounds and directory-scan reuse |
| `clutch.stress-capture` | Repeated pipe overflow/recovery, native Windows handle counts and POSIX child reaping |
| `clutch.stress-integration` | Shared/deep package graphs, cycle recovery, conflict rollback, package limits, serial/parallel C/C++ PCH builds, shared-header invalidation and compile-failure recovery |
| `clutch.integration` / `clutch.posix-integration` | Real C/C++ builds, profiles, flags, incremental behavior, saved tools and cleanup |
| `clutch.file-api` | Query/reply versions, compilation-database agreement, snapshots, failures and concurrency |
| `clutch.targets` | Library/application links, recursive source patterns, noncompiled IDE files, public requirements, target selection, archive replacement, import libraries and DLL staging |
| `clutch.version-ranges` | Semantic precedence, range intersections, prerelease gates and invalid constraints |
| `clutch.dependencies` | Local Git histories, transitive conflicts/backtracking, cycle rejection, locks/offline recovery and C/C++ PCH rebuilds across host generators |
| `clutch.foreign-dependencies` | Local directories, real CMake/Makefile libraries without Clutch manifests, generated headers, public compile flags, mixed-language linking, shared runtime libraries, installed SDKs and source-catalog imports without vcpkg |
| `clutch.package-examples` | Shipped local directory, Git, catalog, Make and installed-package examples; both profiles, consumer tests and locked offline use |
| `clutch.workspace` | Qualified links, member boundaries/standards, locks, stale inputs, relocation and native regeneration |

`tests/scripts/tidy.py` checks enabled clang-tidy diagnostics and compilation
database coverage for owned sources. See [static analysis](CONTRIBUTING.md#static-analysis)
for native database setup and example coverage. Tooling tests exercise missing
commands, generated-source exclusion and invalid database records without LLVM.

Generator tests run available host tools: Ninja/GNU Make on POSIX, Ninja/NMake/
Visual Studio on Windows, and Xcode on macOS. JOM and Windows GNU Make are
exercised when present. Report what ran and any skips. Python powers test
harnesses only; generated project builds do not require it.

File-API regressions also configure/build Unicode source and profile paths,
restore saved actions, relocate the Clutch executable, and search a Unicode
`PATH` entry without relocating external tool installations. POSIX checks ensure
generated metadata and object-file symlinks cannot overwrite source files.
Workspace checks distinguish harmless repeated local
names from conflicting shared-library loader names. Linux target checks move
an artifact tree and execute its shared-library consumer to catch absolute
`DT_NEEDED` paths. Moving saved configuration itself still requires reconfiguration.

Flexible-manifest regressions combine root application shorthand, explicit
libraries and member manifests. They compile local macro/header overrides,
verify discovery excludes sources owned by explicit targets, reject invalid
application links and name conflicts, and compare file-API source commands with
the compilation database. Shared compile settings preserve project-level lock
resolution and are checked with each available native generator.

Tooling tests use disposable repositories and install trees to check source
identity, release-only CI classification, package notices/checksums and failure
handling. The [CI driver](releases.md#continuous-verification) additionally runs
examples in both profiles, verifies installation and builds packages.

## Stress checks

Bounded stress tests run with ordinary CTest and require no network service.
They check correctness and resource cleanup, with process timeouts to catch hangs;
they do not impose machine-dependent performance thresholds.

The mixed-language build case changes a header shared by both PCH prefixes and
checks that every dependent artifact is rebuilt and the linked program computes
the new result. It also injects a compiler error, requires `clutch run` to report
failure, then repairs the source and verifies recovery without cleaning. Both
checks run with one and four compiler jobs.

```sh
ctest --test-dir build-native -L stress --output-on-failure
ctest --test-dir build-native -LE stress --output-on-failure
```

For a larger local package/build fixture, supply your built Clutch and installed
compiler paths. Use a Visual Studio developer terminal for `--cc cl --cxx cl`.

```sh
python tests/integration/stress_integration.py \
  --clutch /absolute/path/to/clutch --cc clang --cxx clang++ --ninja ninja \
  --packages 128 --units 96 --repeats 4 -v
```

Defaults are 48 packages, 24 compile units and two no-op repetitions. Each scale
argument accepts 1–256. The package-limit case separately checks rejection above
the documented 1,024-package bound. The large source-selection fixture uses the
host's temporary filesystem so a container bind mount does not dominate setup.
Fixtures are temporary and cleaned by their suite. These checks supplement normal
integration tests and static analysis;
passing them does not establish that every possible graph or toolchain is correct.

## Package examples

`clutch.package-examples` builds the shipped
[dependency examples](../examples/dependencies/README.md) in debug and release,
runs their independent consumer tests, and verifies locked offline reuse. The
default suite needs no hosted service: it prepares local Git/catalog repositories
and uses the example installed-package prefix. Windows SDK environments also
build the real DirectX example. Fixture setup preserves unowned files.

```sh
ctest --test-dir build-native -R package-examples --output-on-failure
```

To fetch and test the real pinned CLI11 and fmt GitHub releases, run the same
suite with explicit network access. Supply your built executable and native
compiler/build-tool paths:

```sh
python tests/integration/package_examples_integration.py \
  --clutch /absolute/path/to/clutch --cc clang --cxx clang++ --ninja ninja --network -v
```

The command is also available in PowerShell on one line, using `clutch.exe`,
`--cc cl --cxx cl` in a developer terminal and a discoverable Ninja executable.
Add `--cuda` only on a machine with a compatible installed CUDA Toolkit. Without
these opt-in flags, the corresponding tests report explicit skips. Python drives
the checks and sample-repository setup; it is not part of the Clutch executable.
Each example also provides a `[[test]]` entry for direct `clutch test` use.

## Clean fixtures

Preview before removing generated folders in `tests/work`:

```sh
bash tests/scripts/clean-work.sh --dry-run
bash tests/scripts/clean-work.sh
```

In PowerShell:

```powershell
./tests/scripts/clean-work.ps1 -WhatIf
./tests/scripts/clean-work.ps1
```

Cleanup preserves the work root and loose files, and rejects redirected trees.
Close processes using fixtures first; subsequent integration runs recreate them.
