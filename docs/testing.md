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
storage to avoid bind-mount overhead. Ordinary suites remove their own fixtures;
the optional self-hosting check retains its source copies and binaries for inspection.

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

### Self-hosting verification

The root native manifest can build Clutch after the initial CMake bootstrap.
Enable this longer test explicitly on an existing CMake test build:

```sh
cmake -S . -B build-native -DCLUTCH_TEST_SELF_HOSTING=ON
cmake --build build-native --parallel 2
ctest --test-dir build-native -R '^clutch.self-hosting$' --output-on-failure
```

It copies only native source inputs into a fresh fixture, blocks CMake lookup,
builds a native release executable, then uses that executable to build a debug
copy. Both versions must agree with the source archive's `VERSION`; unchanged
rebuilds must retain every object timestamp. The final copy scaffolds, compiles
and runs a separate C++ program. CTest retains outputs and `verification.json`
under its binary test directory; the standalone runner defaults to `tests/work/`.
CMake remains supported for normal development.

The runner also accepts explicit `--clutch`, `--cc`, `--cxx`, `--ninja`, `--jobs`
and `--work-root` arguments. For container checks, choose a work root on the
container's local filesystem to avoid source bind-mount compilation overhead.
This check is off by default so ordinary CI does not compile the entire project
two extra times on every platform. See [native build commands](installation.md#build-clutch-with-clutch).

### Functional suites

`clutch.watch` starts real watchers, edits source/header/manifest inputs and waits
for flushed cycle-completion lines. It checks debounce, source membership,
timestamp-preserving saves, ignored output/cache trees, additional resource paths,
workspace/local dependency roots, test/release selection and failure recovery.
A blocking pre-build hook provides a rendezvous for edits made during compilation;
the next cycle must not lose them. POSIX tests verify idle interrupt status;
Windows cleanup uses a separate process group and CTRL_BREAK. Every subprocess
and rendezvous has a timeout and cleanup path.

`clutch.commands` checks named commands before source discovery, literal/empty
argument forwarding, context tokens, working directories, profile selection,
workspace ownership and ordered configure/build hooks. It verifies no-op/clean
builds, failure exit codes, suppression of later hooks/application execution,
and failed-configuration blocking plus File API error replies. These fixtures use
Python as an explicitly declared user command; the Clutch runner is native.
Run just this suite with:

```sh
ctest --test-dir build-native -R '^clutch.commands$' --output-on-failure
```

`clutch.cmake-projects` exercises the whole-project provider with framework-like
target helpers, generated resources, top-level source identity, preset directories
and environment, multiple configurations, native failure codes, CTest filters,
installation and CPack. It also rejects escaped reply references and stale failed
configuration. The target integration suite separately checks argument forwarding
after repeatable target selection. These fixtures use installed CMake tools and
do not download external libraries.

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
| `clutch.graph` | Graph validation, deterministic DOT escaping, package/action identity separation, generated-header preparation edges and optional rendering |
| `clutch.files` | Generated-file content/timestamps, directory-output rejection, and artifact cleanup containment, preflight, redirection and metadata preservation |
| `clutch.zip` | Archive path traversal, Windows device names, invalid UTF-8, case/hierarchy collisions, member bounds and existing-output preservation |
| `clutch.bundle` | Independent ZIP/CRC parsing, repeatable bytes, Unicode/binary files, exclusions, workspace relocation and offline Git/local/CMake dependency snapshots; includes 256 files and a one-MiB binary fixture |
| `clutch.source-files` | Local/recursive source matching, language-filtered discovery, brace products, canonical paths, nested-project/generated-tree exclusions and invalid patterns without a compiler |
| `clutch.defaults` | Automatic generator/executor selection and fallback, cached choices, explicit override errors, manifest-relative glob depth, and real C/C++/mixed discovery with noncompiled IDE headers |
| `clutch.configured-files` | Template/schema/path limits, Git identity parity, direct native refresh and no-op builds, missing outputs, workspace/dependency namespaces, version constraints, source bundles, concurrent callbacks and Windows delete-pending lock recovery |
| `clutch.tooling-safety` | Independent symbols/optimization, warning-array diagnostics and workspace persistence, policy validation, optional real sanitizer runtimes and lifetime support, formatter scope, analyzer database forwarding and debugger argument/exit handling |
| `clutch.self-hosting` | Optional complete native release/debug generations, source-archive version identity, no-op object preservation and final executable smoke check without CMake |
| `clutch.capture` | Native argv boundaries, exit status, exact four-MiB output boundary and oversized-output rejection |
| `clutch.stress-source-files` | 2,048 source files with 64 alternatives/repeated patterns; expansion bounds and directory-scan reuse |
| `clutch.stress-capture` | Repeated pipe overflow/recovery, native Windows handle counts and POSIX child reaping |
| `clutch.stress-integration` | Shared/deep package graphs, cycle recovery, conflict rollback, package limits, serial/parallel C/C++ PCH builds, shared-header invalidation and compile-failure recovery |
| `clutch.integration` / `clutch.posix-integration` | Real C/C++ builds, profiles, flags, incremental behavior, saved tools and cleanup |
| `clutch.file-api` | Query/reply versions, compilation-database agreement, snapshots, failures and concurrency |
| `clutch.targets` | Library/application links, recursive source patterns, noncompiled IDE files, public requirements, target selection, archive replacement, import libraries and DLL staging |
| `clutch.version-ranges` | Semantic precedence, range intersections, prerelease gates and invalid constraints |
| `clutch.git-submodules` | Offline no-write behavior, unsafe paths, modified revisions and bounded deep traversal without Git/network processes |
| `clutch.dependencies` | Local Git histories, transitive conflicts/backtracking, cycle rejection, locks/offline recovery, ownership records, recursive pinned submodules, dirty/missing nested checkouts, relocated submodule bundles, Windows long paths and permanent source-file handles, and C/C++ PCH rebuilds across host generators |
| `clutch.foreign-dependencies` | Local directories, real CMake/Makefile libraries without Clutch manifests, generated headers, public compile flags, mixed-language interface sources, target discovery/implicit selection, incremental linking, shared runtime libraries, installed SDKs and source-catalog imports without vcpkg; Windows also checks GNU-style Clang/MSVC debug and release runtimes when available |
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

The foreign-dependency suite invokes generated backends directly as well as
through Clutch. It checks shared preparation across parallel consumers, fresh
generated public headers, header-only interface refresh, incremental relinking,
upstream failures and Windows DLL repair. A C consumer links a C++ Make archive
and an explicit installed archive using `link_language = "cpp"`. Submodule
fixtures explicitly permit local file transport only inside their test process;
production resolution retains Git's configured transport policy.

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

Clean-build regressions deliberately corrupt unchanged products and transitive
objects while preserving timestamps. `build/run/test --clean` must repair them,
retain configuration/IDE snapshots and leave unrelated targets/profiles intact.
A failed compilation must prevent program launch. Foreign CMake/Make fixtures
exercise their own forced-build actions. Update-reporting fixtures publish local
Git versions and verify compatible proposals, transitive additions/removals,
conflict backtracking, immutable selectors and unchanged locks/saved graphs.

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

## Feature examples

`clutch.feature-examples` copies the shipped examples into temporary projects and
compiles their actual manifests without network access. It checks generated
headers, automatic/flat/recursive C/C++ discovery, profile combinations, shared
libraries and runtime loading, source-bundle relocation, manual commands and a
live resource-watch cycle. This complements the dependency-specific
`clutch.package-examples` suite above.

```sh
ctest --test-dir build-native -R feature-examples --output-on-failure
```

Optional Node commands and sanitizer variants run when their tools/runtime are
available. Unsupported compiler capabilities report explicit skips after their
configuration rejection is checked. ThreadSanitizer may also report an explicit
skip when host sandbox/ASLR restrictions prevent runtime initialization. Windows
and POSIX shell variants run on their respective hosts. The suite does not claim
to validate Xcode, JOM or external SDKs on hosts without those tools.

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
