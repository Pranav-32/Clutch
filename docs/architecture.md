# Clutch architecture and file guide

Clutch is a C++17 build system for C and C++ applications. It resolves its own
target graph and generates native backend files plus an IDE compilation database.
CMake builds and tests Clutch during development. Foreign dependency adapters
and self-hosting remain separate future work.

This guide inventories maintained sources, headers, setup files, and generated
outputs. Fixtures under `tests/work/` are ignored verification products.

## Layout and ownership

Headers live beside their implementations. `src/` is the private include root;
cross-module includes use architectural paths. Each header describes its module
interface, with private helpers usually in an anonymous namespace in the source.
There is no installed C++ API or separate project include tree.

```text
src/
  main.cpp
  app/                  CLI and project workflows
  core/                 Manifest, graph, configuration storage, IDE metadata
  toolchains/           Compiler discovery and saved compiler settings
  backends/             Generator registry and native build dispatch
    ninja/              Ninja rules
    make/               GNU Make, NMake, and JOM rules
    visual_studio/      Native MSBuild projects and solution
    xcode/              Native macOS projects and schemes
  platform/             Processes, file output, terminal messages
third_party/            Vendored headers and upstream licenses
examples/               Independent projects configured and built with Clutch
tests/                  Verification scripts and ignored fixtures
.devcontainer/          Contributor image and initialization
docs/                   User guides and contributor documentation
```

See [Contributing](CONTRIBUTING.md#code-organization-and-style) for formatting,
C++ conventions and documentation expectations.

## Source and header inventory

### Application layer

| File | Responsibility |
| --- | --- |
| [`src/main.cpp`](../src/main.cpp) | Entry point forwarding arguments to `run_cli`; exports no reusable API and needs no header. |
| [`src/app/cli.hpp`](../src/app/cli.hpp) | Declares the `run_cli` interface. |
| [`src/app/cli.cpp`](../src/app/cli.cpp) | Parses positional configuration and workflow commands, separates configuration overrides from execution, lists generators, selects tests, and propagates errors/status. |
| [`src/app/workflow.hpp`](../src/app/workflow.hpp) | Declares project creation and selected-profile cleanup. |
| [`src/app/workflow.cpp`](../src/app/workflow.cpp) | Implements `new`, `init`, and `clean`, preserving existing sources and verifying build-directory ownership before deletion. |

### Project and configuration core

| File | Responsibility |
| --- | --- |
| [`src/core/options.hpp`](../src/core/options.hpp) | Project/profile/job settings plus configuration-only compiler, generator, and executor overrides. |
| [`src/core/paths.hpp`](../src/core/paths.hpp) | Header-only `clutch::fs` alias for `std::filesystem`. |
| [`src/core/hash_constants.hpp`](../src/core/hash_constants.hpp) | Standard FNV-1a offset bases and primes, documented for stable identifiers and manifest change detection; header-only constants. |
| [`src/core/encoding_constants.hpp`](../src/core/encoding_constants.hpp) | Named unsigned hexadecimal widths/masks and ASCII control boundaries shared by serializers; header-only constants. |
| [`src/core/project.hpp`](../src/core/project.hpp) | Resolved paths, language standards, generator, profile settings, and test specifications. |
| [`src/core/project.cpp`](../src/core/project.cpp) | Validates TOML, discovers/deduplicates sources, resolves standards/includes/definitions, and selects profile, generator, and tests. |
| [`src/core/build_directory.hpp`](../src/core/build_directory.hpp) | Declares profile path resolution, validation, and directory preparation. |
| [`src/core/build_directory.cpp`](../src/core/build_directory.cpp) | Rejects unsafe/redirected/unrelated output paths and verifies/writes ownership markers. |
| [`src/core/build_graph.hpp`](../src/core/build_graph.hpp) | Defines units, executable targets, and the graph with compiler/backend selections. |
| [`src/core/build_graph.cpp`](../src/core/build_graph.cpp) | Constructs compile/link vectors, maps portable settings to compiler flags, and selects C/C++ compilers and link drivers. |
| [`src/core/graph_visualization.hpp`](../src/core/graph_visualization.hpp) | Declares optional Graphviz DOT export without a Graphviz runtime dependency. |
| [`src/core/graph_visualization.cpp`](../src/core/graph_visualization.cpp) | Writes deterministic, escaped source/object/executable nodes and prerequisite edges to the profile build directory. |
| [`src/core/configure.hpp`](../src/core/configure.hpp) | Declares explicit configuration and its resulting graph. |
| [`src/core/configure.cpp`](../src/core/configure.cpp) | Coordinates validation, tools, graph construction, native outputs, IDE metadata, and persisted settings without compilation. |
| [`src/core/configuration_store.hpp`](../src/core/configuration_store.hpp) | Declares configured-graph saving/loading and stale-state handling. |
| [`src/core/configuration_store.cpp`](../src/core/configuration_store.cpp) | Writes `configuration.toml` and validates manifest identity, profile, location, and schema when loading; rejects missing/stale/moved configuration. |
| [`src/core/compilation_database.hpp`](../src/core/compilation_database.hpp) | Declares IDE compilation database generation. |
| [`src/core/compilation_database.cpp`](../src/core/compilation_database.cpp) | Writes escaped JSON `directory`, `file`, `output`, and `arguments` for application/test sources in the profile directory. |

### Compilers and generators

| File | Responsibility |
| --- | --- |
| [`src/toolchains/toolchain.hpp`](../src/toolchains/toolchain.hpp) | Resolved C/C++ executables and driver family; compiler resolution/persistence interface. |
| [`src/toolchains/toolchain.cpp`](../src/toolchains/toolchain.cpp) | Applies overrides/environment/cache/discovery, checks all source languages and compatible driver families, and saves selections. |
| [`src/backends/backend.hpp`](../src/backends/backend.hpp) | Backend identity/executor and generator descriptions; listing, normalization, discovery, persistence, generation, and build dispatch. |
| [`src/backends/backend.cpp`](../src/backends/backend.cpp) | Registry for six generators with host restrictions and native executor discovery; dispatches both generation and build commands. |
| [`src/backends/ninja/ninja.hpp`](../src/backends/ninja/ninja.hpp) | Native Ninja generation interface. |
| [`src/backends/ninja/ninja.cpp`](../src/backends/ninja/ninja.cpp) | Ninja compile/link rules, response files, dependency tracking, and explicit application/test targets with format-specific escaping. |
| [`src/backends/make/make.hpp`](../src/backends/make/make.hpp) | GNU Make/NMake/JOM generation and build-invocation interface. |
| [`src/backends/make/make.cpp`](../src/backends/make/make.cpp) | Appropriate Makefile dialect, response files, GNU compiler dependency files or conservative project-header prerequisites, and target aliases. |
| [`src/backends/visual_studio/visual_studio.hpp`](../src/backends/visual_studio/visual_studio.hpp) | Native Visual Studio generation and MSBuild selection interface. |
| [`src/backends/visual_studio/visual_studio.cpp`](../src/backends/visual_studio/visual_studio.cpp) | Deterministic solution/project IDs, per-source native CL settings, Link settings, header tracking, and CLI traversal selecting application/tests. |
| [`src/backends/xcode/xcode.hpp`](../src/backends/xcode/xcode.hpp) | Native macOS project generation and selected-target xcodebuild interface. |
| [`src/backends/xcode/xcode.cpp`](../src/backends/xcode/xcode.cpp) | Deterministic OpenStep projects, native C/C++ executable targets, source flags, profile settings, and shared schemes using Apple's Clang. |

`[build].generator` and configuration's `--generator` select build-file format
and native backend. `--build-tool` locates its executor. `--cc`/`--cxx` select
compiler drivers. Toolchain means compiler/linker/SDK/target environment, not
Ninja or an IDE generator. Full cross-compilation settings remain future work.

### Platform services

| File | Responsibility |
| --- | --- |
| [`src/platform/process.hpp`](../src/platform/process.hpp) | Executable discovery, argument quoting, synchronous child execution. |
| [`src/platform/process.cpp`](../src/platform/process.cpp) | Explicit-path/`PATH` discovery; Windows `CreateProcessW` or POSIX `fork`/`execvp`, inherited environment/console, and child status. |
| [`src/platform/files.hpp`](../src/platform/files.hpp) | Shared `write_if_changed` output interface. |
| [`src/platform/files.cpp`](../src/platform/files.cpp) | Parent creation and checked output, preserving timestamps for identical text. |
| [`src/platform/terminal.hpp`](../src/platform/terminal.hpp) | Shared status logging interface. |
| [`src/platform/terminal.cpp`](../src/platform/terminal.cpp) | Concise terminal-aware ANSI status output and `NO_COLOR` support. |

### Vendored code

| File | Responsibility |
| --- | --- |
| [`third_party/CLI11.hpp`](../third_party/CLI11.hpp) | Upstream single-header argument parser. |
| [`third_party/CLI11.LICENSE`](../third_party/CLI11.LICENSE) | CLI11 license. |
| [`third_party/toml.hpp`](../third_party/toml.hpp) | toml++ single-header manifest/configuration parser. |
| [`third_party/tomlplusplus.LICENSE`](../third_party/tomlplusplus.LICENSE) | toml++ license. |
| [`third_party/README.md`](../third_party/README.md) | Pinned versions, licenses, roles, include paths, and update guidance. |

Keep upstream implementations/licenses intact and scope formatting/lint fixes to
owned code. Building Clutch requires no dependency downloads.

## Configuration lifecycle

`clutch .` (or `clutch path/to/project`) performs configuration:

1. Validate the manifest, source set, standards, tests, and profile.
2. Resolve/validate the directory from `profile.*.build_dir`.
3. Resolve the generator, its native executor, and compatible compilers.
4. Construct the backend-independent compile/link graph.
5. Prepare owned output directories and generate native build files.
6. Write IDE metadata and compiler/backend selection caches.
7. Save the graph and manifest signature.

Configuration creates files and discovers tools without compiling project sources.
`--release` selects the release profile. Generator/compiler/executor overrides
belong to this phase; builds consume saved state.

| Profile-directory output | Purpose |
| --- | --- |
| `.clutch-build-owner` | Canonical project-root ownership marker. |
| `configuration.toml` | Resolved graph, executables, profile/location, and manifest change detector. |
| `compile_commands.json` | Absolute per-source arguments, including tests. |
| `build-graph.dot` | Source-to-object-to-executable graph for optional Graphviz inspection. |
| `toolchain.toml` | Compiler choices for subsequent explicit configuration. |
| `backend.toml` | Saved generator/executor settings. |
| `build.ninja` | Ninja rules when selected. |
| `Makefile` and response files | GNU Make, NMake, or JOM rules when selected. |
| `clutch.sln`, `clutch_target_N.vcxproj`, `clutch.proj` | Visual Studio solution, native projects, and explicit CLI target traversal. |
| `<project>.xcodeproj/` | Native Xcode project/shared schemes. |
| `obj/`, backend intermediates, debug data | Compiler and backend products after building. |
| `<project>` or `<project>.exe` | Linked application. |
| `tests/` | Linked test executables. |

Only the selected generator's files are emitted in a configuration pass. IDEs
read the active build directory; no root database copy/symlink is created.
Visual Studio/Xcode projects contain one active profile. Configure debug/release
independently to create separate projects.

## Build and project workflows

- `build` loads the saved graph, validates its profile/location/manifest, and
  invokes the selected native backend for the application.
- `run` builds and launches the application from the project root, forwarding
  arguments and returning its exit code.
- `test` builds selected test executables and aggregates runtime failures.
  A substring filter limits selection.
- `clean` verifies ownership/redirection before removing the profile directory.
  Configure again before building afterward.
- `new` creates a new project; `init` preserves sources and refuses an existing
  manifest. Both require explicit configuration before building.
- `generators` lists implemented generator names and host availability.

Build execution accepts project/profile/job settings, not generator or compiler
overrides. Source/header edits are incremental. Manifest changes and additions
or removals of discovered sources require explicit configuration. Direct
Ninja/GNU Make builds can automatically rerun positional Clutch configuration
when the manifest timestamp changes, using the recorded tool selections. This
direct-backend regeneration differs from `clutch build`, which rejects stale
saved configuration. Neither workflow automatically discovers source additions
or removals without configuration.

## Invariants and limits

- Graph/database paths are absolute; manifest paths resolve from the project root.
- Build paths belong in profiles, defaulting to `Clutch-build-debug` and
  `Clutch-build-release`; there is no CLI build-directory override.
- Outputs cannot replace source/reserved directories, adopt unrelated contents,
  or redirect through symlinks/junctions. Cleanup verifies ownership.
- `.clutch/` is reserved for the project-local dependency workspace, excluded
  from source control, Docker contexts, and conservative header scans. Dependency
  installation remains future work.
- C/C++ sources use the correct compiler; C++ targets use the C++ link driver.
- Tests inherit standards/includes/definitions and provide independent `main`s.
- Graph arguments define compilation behavior; generators and IDE commands must
  preserve it. A generator cannot silently substitute an unrelated compiler.
- Configuration precedence is compiler overrides, environment, saved selections,
  then discovery. Executor precedence is `--build-tool`, `CLUTCH_BUILD_TOOL`,
  backend-specific environment, saved selection, then discovery.
- `ninja`/`make` are host-general. `nmake`/`jom`/`visual-studio` are Windows-only.
  Visual Studio requires MSVC; Xcode requires macOS, full Xcode, and Apple Clang.
  NMake is serial. Make-based MSVC header tracking is conservative; compiler
  installation changes require a clean build.
- The graph supports one application and explicit executable tests. Libraries,
  dependency adapters, arbitrary custom commands, cross-compilation configuration,
  and full CMake parity remain future work. Unsupported dependencies fail explicitly.
- Windows runtime checks do not validate macOS generators. Docker/Linux checks
  passed with Clang 22; IDE attachment and native Xcode execution remain unvalidated.

## Extension points

The build graph is validated before generation and after loading persisted state.
Duplicate output producers are rejected, and a topological traversal rejects
cycles. Shared source inputs with separate target objects remain valid. This
validation covers compile/link artifact edges; package resolution and discovered
header graphs are separate future work. Graphviz rendering is diagnostic and
does not establish build performance or artifact size.

Resolve manifest features in `core/project.*` and carry values into the graph.
Extend target kinds/compiler behavior before serializing new semantics in each
generator. Derive IDE commands from the same graph, and update the store schema
when persisted semantics change.

A backend owns generation, discovery, host restrictions, and invocation through
the registry. The CLI does not assume Ninja flags. Register only implemented
generators with executable build behavior.

Future foreign CMake/Make/Blaze adapters should import artifacts, public includes,
definitions, transitive link requirements, and preparation dependencies while
preserving foreign build rules. Clutch remains the configuration engine. A
compilation database alone cannot describe a complete target/link model.

## Setup and verification inventory

Shared project paths live in `src/core/project_layout.hpp`: manifest, profile
directory prefix, ownership marker, persisted state and generated metadata.
Configuration, scaffolding and generators use these names together. Compiler
flags and generator format tokens stay with their respective drivers.

The root VERSION is the release identity source. `cmake/ClutchVersion.cmake`
validates it and combines it with Git provenance; `cmake/GenerateVersion.cmake`
is the build-time entry point. `src/core/version.hpp.in` becomes a private
generated header in the build tree. The installed native CLI embeds that string
and never requires Git at runtime. See [versions and releases](releases.md).

| File | Purpose and maintenance |
| --- | --- |
| [`CMakeLists.txt`](../CMakeLists.txt) | C++17 development target, private/vendor include roots, warnings, static MSVC runtime, executable/license installation, and CTest registration. Synchronize its source list with new modules. |
| [`VERSION`](../VERSION), [`cmake/ClutchVersion.cmake`](../cmake/ClutchVersion.cmake), [`cmake/GenerateVersion.cmake`](../cmake/GenerateVersion.cmake), [`src/core/version.hpp.in`](../src/core/version.hpp.in) | Canonical release version, Git/source-archive identity, changed-content generation and CLI header template. |
| [`src/core/project_layout.hpp`](../src/core/project_layout.hpp) | Shared layout contract and default profile-directory policy. |
| [`.github/workflows/ci.yml`](../.github/workflows/ci.yml), [`build.yml`](../.github/workflows/build.yml) | Push/PR checks, reusable native matrix, container verification and downloadable packages. |
| [`.github/workflows/release.yml`](../.github/workflows/release.yml) | Tagged-commit validation, full native verification, checksums and draft-first release publication. |
| [`.github/workflows/version.yml`](../.github/workflows/version.yml), [`release-please-config.json`](../.github/release-please-config.json), [`release-please-manifest.json`](../.github/release-please-manifest.json) | Conventional Commit version/changelog proposals and automation state; no automatic merge or publication. |
| [`.github/scripts/ci.py`](../.github/scripts/ci.py), [`release.py`](../.github/scripts/release.py) | Native build/test/install driver and release validation, archives, license checks, streaming checksums and publication gates. Python is CI tooling only. |
| [`tests/test_release.py`](../tests/test_release.py), [`test_versioning.py`](../tests/test_versioning.py) | Packaging/identity/failure regressions and real disposable Git fixtures for tags, dirty files, shallow clones, worktrees and no-Git builds. |
| [`examples/single-file/clutch.toml`](../examples/single-file/clutch.toml), [`main.cpp`](../examples/single-file/main.cpp) | C++17 single-file entry source and separate debug/release profiles. |
| [`examples/multi-file/clutch.toml`](../examples/multi-file/clutch.toml), [`src/main.cpp`](../examples/multi-file/src/main.cpp), [`greeting.hpp`](../examples/multi-file/src/greeting.hpp), [`greeting.cpp`](../examples/multi-file/src/greeting.cpp), [`tests/greeting_test.cpp`](../examples/multi-file/tests/greeting_test.cpp) | Explicit application/test sources, colocated headers, shared implementation and runtime arguments. |
| [`examples/c-project/clutch.toml`](../examples/c-project/clutch.toml), [`main.c`](../examples/c-project/main.c) | C11 executable built with a C compiler. |
| [`examples/mixed/clutch.toml`](../examples/mixed/clutch.toml), [`src/main.cpp`](../examples/mixed/src/main.cpp), [`double_value.h`](../examples/mixed/src/double_value.h), [`double_value.c`](../examples/mixed/src/double_value.c) | Recursive C/C++ discovery, separate standards and C linkage across languages. |
| [`tests/integration.ps1`](../tests/integration.ps1) | Windows C/C++/mixed fixtures for configuration, databases, profiles, incremental builds, saved tools, workflows, failures, and ownership. Retains ignored fixtures. |
| [`tests/graph_tests.cpp`](../tests/graph_tests.cpp) | Graph safety regression checks, shared-input handling, deterministic DOT escaping and optional real Graphviz SVG rendering. |
| [`tests/clean-work.ps1`](../tests/clean-work.ps1) | Removes generated folders inside `tests/work` with preview support, containment checks, and link/junction rejection; preserves the work root and loose files. |
| [`tests/clean-work.sh`](../tests/clean-work.sh) | Bash 3.2-compatible cleanup for macOS, Linux and containers, with dry-run support, target preflight and link rejection; preserves the work root and loose files. |
| [`tests/verify.sh`](../tests/verify.sh) | Shared Linux/macOS/container CMake build and CTest runner; resolves compilers/tools, requires the POSIX integration test, and supports configurable jobs and a separate container build tree. |
| [`tests/windows-verify.cmd`](../tests/windows-verify.cmd) | Initializes supplied vcvars64 and runs MSVC/Clang integration checks; accepts Ninja and optional Clutch paths and propagates failures. |
| [`tests/windows-standalone.ps1`](../tests/windows-standalone.ps1) | Copies only the executable into a unique fixture and checks help/version/startup with Windows system paths only, using bounded process waits. |
| [`tests/native-generators.ps1`](../tests/native-generators.ps1) | Native Windows generator fixtures for configuration, build/test execution, paths with spaces, incremental behavior, and header tracking. |
| [`tests/posix-integration.sh`](../tests/posix-integration.sh) | Real C/C++/mixed Ninja and GNU Make checks on POSIX, including workflows, standards/warnings, IDE databases, incremental builds, profiles, errors and ownership; registered by CMake when the required tools are found. |
| [`.gitignore`](../.gitignore) | Ignores generated trees, fixtures, root databases, and local IDE state. Custom output paths may need rules. |
| [`.clang-tidy`](../.clang-tidy) | Advisory first-party analysis using an analysis build directory's database. |
| [`.clang-format-ignore`](../.clang-format-ignore) | Excludes generated trees, fixtures and vendored code from formatting. |
| [`.devcontainer/devcontainer.json`](../.devcontainer/devcontainer.json) | Shared development image, source mount, nonroot user, IDE settings, and initialization. |
| [`.devcontainer/Dockerfile`](../.devcontainer/Dockerfile) | Consistent Ubuntu development environment across host machines, including compiler/build/debug/LLVM tools and CLion backend dependencies. |
| [`.devcontainer/packages.txt`](../.devcontainer/packages.txt), [`install-packages.sh`](../.devcontainer/install-packages.sh), [`create-user.sh`](../.devcontainer/create-user.sh) | Commented distribution package inventory, verified LLVM installation and reusable nonroot account setup. |
| [`.devcontainer/post-create.sh`](../.devcontainer/post-create.sh) | Trusts only the mounted Git checkout inside Docker, then delegates to the shared verification runner with Clang, `build-container`, and IDE compilation-database export enabled. |
| [`.devcontainer/.dockerignore`](../.devcontainer/.dockerignore) | Admits only image build metadata from the tools-only `.devcontainer/` build context; project sources are mounted at runtime. |
| [`.gitattributes`](../.gitattributes) | Text normalization, LF for Linux setup files, and binary artifact rules. |
| [`docs/CONTRIBUTING.md`](CONTRIBUTING.md) | Contributor environment, formatting, C++ conventions, validation, and review workflow. |
| [`README.md`](../README.md) | Project overview, current capabilities, installation and quick start for users. |
| [`LICENSE`](../LICENSE) | BSD 3-Clause terms and Clutch copyright notice. Preserve with redistributions. |
| [`docs/licensing.md`](licensing.md) | Copyright, source/binary redistribution, upstream notices, packaging and contribution rules. |
| [`docs/clion-copyright.md`](clion-copyright.md) | Maintainer copyright profile, owned-source scope, formatting, and save/commit automation. |
| [`docs/clion-copyright-template.txt`](clion-copyright-template.txt) | Copyable Velocity notice with preserved first year, current year and BSD-3-Clause SPDX identifier. |
| [`docs/installation.md`](installation.md) | Platform prerequisites, CMake source builds, installation prefixes and compiler environments. |
| [`docs/usage.md`](usage.md) | Project configuration, generators, compilers, IDE metadata and executable tests. |
| [`docs/examples.md`](examples.md) | Running the checked-in examples with installed or locally built Clutch binaries. |
| [`docs/testing.md`](testing.md) | Contributor verification commands, runner settings, fixture cleanup and validation scope. |
| [`docs/architecture.md`](architecture.md) | This inventory and lifecycle guide. |
| [`docs/static-analysis.md`](static-analysis.md) | clang-tidy policy, compatibility checks, databases, and reviewed fixes. |
| [`docs/manifest.md`](manifest.md) | Complete implemented manifest schema, defaults, validation, compiler mappings, examples and migration rules. |
| [`docs/development-container.md`](development-container.md) | Docker prerequisites, CLion/VS Code setup, direct Docker commands, maintenance, and validation status. |
| [`docs/releases.md`](releases.md), [`docs/CHANGELOG.md`](CHANGELOG.md) | Version policy, automation setup, native support matrix, release procedure and generated release history. |

The root `.clang-format` is already documented and omitted from this inventory.
The Docker image and Linux post-create workflow passed verification on 2026-10-18
with Clang 22, including POSIX integration and Graphviz. GCC 15 and Windows MSVC
also passed native verification, examples and packaging. Git version and release
tooling have separate regression checks. IDE attachment, hosted workflow runs
and native macOS/Xcode execution remain separate validation work.
