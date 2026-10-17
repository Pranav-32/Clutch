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
CMake/CTest, Ninja, GNU Make, and C/C++ compilers before running it. Integration
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

The graph supports one application executable and explicit executable tests.
Git dependencies, CMake/Make/Blaze dependency adapters, library targets, arbitrary
custom commands, cross-compilation settings, PCH, project installation rules, automatic updates,
and complete CMake capability coverage remain future work. Foreign adapters will
configure external libraries while Clutch keeps its own configuration engine.

The Docker image and Linux initialization workflow have been validated with
Clang 22 on `linux/amd64`: all four CTest checks passed, including Graphviz and
the Ninja/GNU Make integration suite. The manifest guide's examples also built
successfully. IDE attachment and native Xcode execution still require validation
with their respective clients and host tools.
