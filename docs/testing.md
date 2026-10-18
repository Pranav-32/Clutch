# Testing Clutch

These workflows validate Clutch itself. To declare and run tests in a project
built with Clutch, see [project workflows](usage.md#project-workflows). Run the
following commands from the checkout root.

## Linux, macOS and containers

On Linux or macOS, run the complete native verification workflow:

```bash
bash tests/verify.sh

# Select compilers and adjust parallelism when needed:
CC=clang CXX=clang++ CLUTCH_BUILD_JOBS=4 \
    CLUTCH_TEST_BUILD_DIR=build-native-clang bash tests/verify.sh
```

The script builds Clutch in `build-native`, runs all registered CTest checks,
and requires the POSIX Ninja/GNU Make integration suite to be present. Install
CMake/CTest, Ninja, GNU Make, Python 3.9+ and C/C++ compilers before running it. Integration
checks cover scaffolding, mixed C/C++, test execution, argument forwarding,
profiles, C++17 and warning settings, IDE databases, incremental/header rebuilds,
saved configuration, and safe cleanup. The container's initialization script calls
this same runner with `build-container` and compilation-database export enabled:

```bash
bash .devcontainer/post-create.sh
```

The native runner accepts these environment settings:

| Setting | Default | Purpose |
| --- | --- | --- |
| `CC` / `CXX` | `cc` / `c++` | C/C++ executable names or paths; arguments are not accepted |
| `CLUTCH_BUILD_JOBS` | `2` | Positive number of parallel compiler jobs |
| `CLUTCH_TEST_BUILD_DIR` | `build-native` | CMake verification build tree; relative paths resolve from the checkout |
| `CLUTCH_TEST_EXPORT_DATABASE` | `OFF` | `ON` exports the CMake database for developing Clutch |

Use separate directories when switching host or compiler. Container initialization
defaults to Clang and selects `build-container` with database export enabled.
These runner settings build Clutch itself; a project's Clutch build directories
remain defined by its manifest profiles.

## Windows

Run CTest for the CMake bootstrap build. On Windows, register compiler-backed
checks with `-DCLUTCH_TEST_NINJA="path/to/ninja.exe"`, then run:

```powershell
ctest --test-dir build -C Release --output-on-failure
./tests/integration.ps1 -Clutch ./build/Release/clutch.exe -Ninja path/to/ninja.exe
./tests/integration.ps1 -Clutch ./build/Release/clutch.exe -Ninja path/to/ninja.exe -CC clang -CXX clang++
```

`tests/windows-verify.cmd <vcvars64.bat> <ninja.exe> [clutch.exe]` initializes the
developer environment and runs MSVC/Clang checks. Integration checks cover C,
C++, mixed projects, databases, spaces, profiles, incremental builds, header
edits, saved configuration, workflows, and failures. Validate platform-specific
generators on their native hosts. `tests/native-generators.ps1` exercises Windows
native generators, and `tests/posix-integration.sh` covers Ninja/GNU Make on a
POSIX host with real C/C++ compilers.

## File-API checks

`clutch.file-api` runs an independent Python consumer against the native CLI.
It covers query-only first configuration, shared and multiple IDE clients,
strict JSON and version diagnostics, profile isolation, exact compilation
database arguments, failure/recovery, immutable generations, writer locks,
concurrent readers and emitted build/run actions. POSIX symlinks and Windows
junctions are checked on their respective hosts. Metadata is also inspected
with additional available native generators. The reference client is exercised
against successful and stale snapshots.

```sh
ctest --test-dir build -R file-api --output-on-failure
```

Use `-C Release` with a multi-configuration CMake generator. CMake registers this
suite when Python 3.9+, Ninja and a C compiler are available; CI and the POSIX
verification runner require registration, so missing dependencies cannot produce
a false successful verification. Python is not a Clutch runtime dependency.
The JSON schema documents are parsed and their local references are checked;
the test runner does not claim to be a complete JSON Schema validator.

## Clean generated test workspaces

Remove generated folders under `tests/work` when you want fresh test workspaces:

Linux, macOS, or the development container:

```bash
bash tests/clean-work.sh --dry-run
bash tests/clean-work.sh
```

Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/clean-work.ps1 -WhatIf
powershell -NoProfile -ExecutionPolicy Bypass -File tests/clean-work.ps1
```

The first command in each pair previews cleanup. Both scripts preserve loose
files and the `work` directory, and reject links inside folders being removed.
Close processes using those folders before cleaning; integration tests create
fresh workspace folders automatically on their next run.

## Scope and validation status

`clutch.workspace` exercises virtual and real-root workspaces, cross-member static
and shared links, local link scoping, member language standards, program working
directories, qualified selection, compilation-database/file-API agreement, stale
member rejection, portable lock bytes, unchanged timestamps, `--locked` failures,
writer exclusion, relocation, and invalid member/cycle handling. It runs the
available native generators on each host. POSIX additionally rejects symlinked
members and lock files. CI and the POSIX verification runner require registration.

```sh
ctest --test-dir build -R workspace --output-on-failure
```

The example runner also verifies generated locks in debug and release. Lock
checks cover double-quote migration/escaping, frozen preservation and local
identities/links; they do not test Git downloads or commit
pinning because those features are not implemented. See [lock files](lock-files.md).

GitHub CI and release jobs share a native verification/packaging runner. Python
tooling tests cover version identities with real temporary Git repositories,
archive notices and checksums, and failed-publication behavior. See
[versions and releases](releases.md) for the platform matrix and commands.

The graph supports executable, static-library, shared-library and test targets.
`clutch.targets` checks native generator builds, named/default selection, public
usage requirements, diamond/static link order, C consumers of C++ archives,
archive member removal, runtime DLL staging and missing import-library repair.
It also verifies file-API kinds/dependency IDs and saved target restoration.
Target fixtures locate their profile through `clutch locate`, follow code-model
references by kind and use published run/artifact paths. This includes a profile
outside the source tree and build directories containing spaces; archive/import
names are not inferred from one compiler's naming convention. Native Visual
Studio checks select Microsoft's CL driver; clang-cl remains covered by other
compatible generators when selected for testing.
Each native generator uses an independent source/build fixture, so one failed
subtest cannot leave optional targets or archives behind for the next generator.
POSIX Make regressions force equal timestamps across response files and build
products to verify archive-member removal and changed definitions. They also
check that unchanged configuration preserves a static archive.
The changed-definition case wraps real compilers with a test-only clock shim that
gives newly compiled objects the previous timestamp. Only the provider's private
definition changes; consumer commands remain identical. This verifies invalidation
through a static-library diamond and its shared/executable consumers even when
Make cannot distinguish old and new object timestamps. An unchanged reconfiguration
must keep the linked consumer's timestamp. Python powers this test shim only;
generated user builds continue to use native Clutch actions.
Ninja and GNU Make run on Linux; Ninja, NMake and Visual Studio run with MSVC on
Windows. macOS additionally exercises Xcode when installed. Python is used by
the test harness, not by Clutch or any generated build action.

Git dependencies, CMake/Make/Blaze dependency adapters, arbitrary
custom commands, cross-compilation settings, PCH, project installation rules, automatic updates,
and complete CMake capability coverage remain future work. Foreign adapters will
configure external libraries while Clutch keeps its own configuration engine.

The v0.2.0 release passed the hosted Linux GCC/Clang, Windows MSVC and both macOS
architecture jobs, including native generator and packaging checks. Executable
discovery preserves driver symlink names such as `clang++`, with a dedicated
regression test. Tooling tests cover Git versioning and release packaging;
workflow syntax is checked with actionlint.

Local tests cover Windows MSVC and Linux Clang; native
macOS feature validation runs in the hosted matrix after pushing. Interactive
IDE attachment requires validation with the respective client.

Debug-symbol output was also checked manually on Windows MSVC with Ninja and
Visual Studio: the workspace application and test each produced a linker PDB.
This is not a cross-platform sidecar-regeneration guarantee or an automated
symbol-artifact test. Explicit symbol-output tracking remains future work; see
[debug symbols](manifest.md#debug-symbols).
