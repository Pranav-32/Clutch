# Testing Clutch

Run commands from the checkout root. These checks develop Clutch itself;
tests in projects built by Clutch use the [project workflow](usage.md#project-workflows).

| Directory | Purpose |
| --- | --- |
| `tests/unit/` | Native graph and safety checks |
| `tests/integration/` | Compiler-backed workflows, generators, file API and workspaces |
| `tests/tooling/` | CI, release packaging and Git versioning |
| `tests/scripts/` | Verification runners and fixture cleanup |
| `tests/work/` | Ignored generated fixtures |

## Linux, macOS and containers

Install CMake/CTest, Bash, Python, Ninja, GNU Make and C/C++ compilers. Python
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
| `clutch.integration` / `clutch.posix-integration` | Real C/C++ builds, profiles, flags, incremental behavior, saved tools and cleanup |
| `clutch.file-api` | Query/reply versions, compilation-database agreement, snapshots, failures and concurrency |
| `clutch.targets` | Library/application links, public requirements, target selection, archive replacement, import libraries and DLL staging |
| `clutch.workspace` | Qualified links, member boundaries/standards, locks, stale inputs, relocation and native regeneration |

Generator tests run available host tools: Ninja/GNU Make on POSIX, Ninja/NMake/
Visual Studio on Windows, and Xcode on macOS. JOM and Windows GNU Make are
exercised when present. Report what ran and any skips. Python powers test
harnesses only; generated project builds do not require it.

File-API regressions also configure/build Unicode source and profile paths,
restore saved actions, and select an executor through a Unicode environment
path. POSIX checks ensure generated metadata and object-file symlinks cannot
overwrite source files. Workspace checks distinguish harmless repeated local
names from conflicting shared-library loader names. Linux target checks move
an artifact tree and execute its shared-library consumer to catch absolute
`DT_NEEDED` paths. Moving saved configuration itself still requires reconfiguration.

Tooling tests use disposable repositories and install trees to check source
identity, release-only CI classification, package notices/checksums and failure
handling. The [CI driver](releases.md#continuous-verification) additionally runs
examples in both profiles, verifies installation and builds packages.

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
