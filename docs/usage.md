# Using Clutch

Configure and build C and C++ projects with Clutch. For your first project, see
the [quick start](../README.md#quick-start); for every manifest field, see the
[manifest reference](manifest.md).

## Configure, then build

Configuration uses the project directory as the positional argument:

```sh
clutch new hello
clutch hello
clutch build --project hello
clutch run --project hello

# From inside an existing project:
clutch .
clutch build --jobs 4
clutch run -- "argument with spaces" --program-flag

# Configure and build the separate release profile:
clutch . --release
clutch build --release --jobs 4
```

`clutch .` validates the manifest, discovers sources and tools, and writes build
files without compiling. `build`, `run`, and `test` load the saved configuration
and execute the selected native backend. They require configuration first.
Changes to `clutch.toml` require another explicit `clutch .` invocation; adding
or removing discovered sources also requires reconfiguration. Editing an
existing source or header only requires a build.

Configuration creates or updates `clutch.lock` at the source/workspace root.
Commit it with the manifests. Use `clutch . --locked` (or add `--release`) to
require an existing matching lock without rewriting resolution. This flag is
configuration-only. Changes to any root/member manifest or the lock invalidate
saved builds; see [lock files](lock-files.md) for its current local-only scope.

Choose compilers and the generator during configuration:

```sh
clutch . --generator ninja --cc clang --cxx clang++
clutch . --generator make --build-tool gmake
clutch generators
```

Compiler overrides are `--cc`/`--cxx`, followed by `CC`/`CXX`, saved compiler
selections, then discovery. `--generator` overrides `[build].generator`.
`--build-tool` overrides the backend executable's location; it accepts an
executable name/path, not a shell command or argument string. The equivalent
generic environment override is `CLUTCH_BUILD_TOOL`; backend-specific fallbacks
are `NINJA`, `MAKE`, `NMAKE`, `JOM`, `MSBUILD`, and `XCODEBUILD`.

On Windows, Ninja discovery also checks the active Visual Studio installation,
bundles beside CMake on `PATH`, and standard Visual Studio/CLion installation
directories under Program Files. The resolved executable is saved in the
selected profile's build directory. A fresh build directory runs discovery
again; use `--build-tool` for a custom installation that is not discoverable.

Resolved settings and the complete graph are saved inside the profile build
directory. Build execution accepts `--project`, `--release`, and `--jobs`; change
compilers, generator, or build-tool paths by configuring again. `run` additionally
forwards program arguments, and `test --filter text` selects tests.

## A project

For several manifest directories, add root `[workspace].members` and configure
the workspace root. Use qualified target names such as
`clutch run --target hello::hello`. Root profiles/generator coordinate all members;
each member keeps its own source paths and standards. Programs/tests run from
their owning member directory. See [workspaces](workspaces.md) for layout and
member restrictions. Commands do not automatically search upward for a workspace.

The [complete `clutch.toml` reference](manifest.md) lists every supported
field, type, default, accepted value and compiler-flag mapping, with complete
examples for standards, profiles, sources and tests.

| Table | Supported fields |
| --- | --- |
| `[project]` | `name`, `version`, `language`, `std`, `entry` |
| `[workspace]` | `members` |
| `[build]` | `generator`, `sources`, `include_dirs`, `defines` |
| `[profile.debug]`, `[profile.release]` | `build_dir`, `optimize`, `warnings` |
| `[[target]]` | `name`, `type`, `sources`, `include_dirs`, `public_include_dirs`, `defines`, `public_defines`, `links`, `public_links`, `default` |
| `[[test]]` | Target fields except `default`; executable only |

```toml
[project]
name = "hello"
version = "0.1.0"
language = "cpp"        # c, cpp (or c++), mixed, auto
std = 20                # Integer or quoted language-standard version

[build]
generator = "ninja"     # Native build-file format/backend
# Optional: otherwise discover .c/.cc/.cpp/.cxx recursively under src/.
sources = ["src/main.cpp"]
include_dirs = ["headers"]
defines = ["APP_VERSION=1"]

[profile.debug]
build_dir = "Clutch-build-debug"

[profile.release]
build_dir = "Clutch-build-release"
optimize = "speed"       # none, speed, size
warnings = "extra"       # none, default, extra
```

A generator chooses native build files and their executor. A toolchain means
the compiler, linker, SDK, and target environment; Ninja and Visual Studio are
generator choices. Select compiler executables separately with `--cc`/`--cxx`.
A comprehensive cross-compilation toolchain schema remains future work.

Paths resolve relative to the manifest. Headers can live beside source files;
an `include/` directory is optional and added automatically when present.
`project.entry` can add an entry source. Without explicit targets, application
sources form one executable. `version` is metadata. Use `[[target]]` for multiple
executables and static/shared libraries; see the [target reference](manifest.md#target).

`warnings = "none"` suppresses compiler warnings with `-w` on GCC/Clang or
`/W0` on MSVC-style drivers. Errors remain errors. `default` enables `-Wall`
or `/W3`, and `extra` adds `-Wextra` or selects `/W4`.

`language` defaults to `auto`. C defaults to std 11 and C++ to std 17. Explicit
`c` or `cpp` must match the application's sources; use `mixed` for both. A C
project can declare `language = "c"` and `std = 17`. Mixed projects use:

```toml
[project]
name = "mixed_app"
version = "0.1.0"
language = "mixed"
std = { c = 11, cpp = 20 }
```

Clutch compiles `.c` files as C and `.cc`/`.cpp`/`.cxx` files as C++, choosing the
C++ linker driver for targets containing C++. Accepted C standards are 99, 11,
17, and 23, subject to compiler support; MSVC C supports 11 and 17 here. Accepted
C++ standards are 11, 14, 17, 20, and 23. The old `edition` and `c_standard`
manifest fields produce migration diagnostics.

C++11 projects also need a standard library that supports C++11 mode, such as
GCC's libstdc++. On Windows, Clang targeting MSVC uses Microsoft's standard
library; select C++14 or newer for that combination. Microsoft's compiler also
[starts at C++14](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version).

The project metadata table is `[project]`. Older `[package]` manifests receive
a rename instruction. After changing a manifest or upgrading from an older
saved configuration, run `clutch .` again to regenerate the build files and
`compile_commands.json` with the current standard and profile settings.

Direct Ninja and GNU Make invocations can regenerate their configuration when a
root or member manifest is newer than the generated build file. These tools use
file timestamps: edits that preserve timestamps, or land within the same clock
tick, require an explicit `clutch .`. Clutch checks input contents when loading
saved configuration and rejects stale inputs even when their timestamps match.

## Generators

`clutch generators` lists implemented generators and their host availability.

| Generator | Native outputs | Host and compiler requirements |
| --- | --- | --- |
| `ninja` | `build.ninja` | Ninja 1.10+; GCC/Clang or MSVC-style drivers on a matching host |
| `make` | `Makefile` | GNU Make, including compatible MinGW/MSYS variants; host compiler and shell environment |
| `nmake` | `Makefile` | Windows and Microsoft NMake; initialized Windows compiler environment |
| `jom` | `Makefile` | Windows and JOM; initialized Windows compiler environment |
| `visual-studio` | `clutch.sln`, `.vcxproj`, `clutch.proj` | Windows, MSBuild with C++ tools, and MSVC `cl` |
| `xcode` | `<project>.xcodeproj` and shared schemes | macOS, full Xcode, and Apple's Clang |

Generators emit the selected debug or release profile. Configuring both creates
independent projects in their respective build directories; these are not
combined multi-configuration projects. Visual Studio uses native CL/Link tasks,
and Xcode uses native C/C++ targets. Neither delegates the main project to CMake
or Ninja. These generators do not imply support for every CMake capability or
every CMake generator variant.

Direct Ninja or GNU Make execution is also supported after configuration:

```sh
ninja -C Clutch-build-debug
make -C Clutch-build-debug
```

Use the command matching the configured generator. Direct Ninja/GNU Make builds
can rerun Clutch configuration when the manifest timestamp changes, reusing the
recorded compiler/generator selections. The `clutch build` workflow instead
requires explicit reconfiguration after manifest changes. Adding or removing
discovered sources requires explicit configuration with either workflow.
NMake executes serially; `--jobs` controls parallelism where supported.
Make-based MSVC builds conservatively track project headers instead of precise
per-unit compiler dependency files.

When a Make compile/link command changes during configuration, Clutch invalidates
its old object and affected target/consumer outputs before updating the response
file. Invalidation follows dependencies through libraries and executables, including
staged Windows DLL copies. This prevents stale flags or archive members when Make
observes equal file timestamps, even after recompiling an object within the same tick.
Unchanged commands keep existing outputs for incremental builds. Configuration
still performs no compilation; the next build recreates invalidated outputs.

## Build directories and IDEs

Debug builds generate symbols automatically. MSVC-style executable/DLL links
produce PDBs; GCC/MinGW and Unix Clang normally embed debug information instead.
Static libraries retain object symbols. Symbol-file paths and sidecar tracking
are not yet configurable; see [debug symbols](manifest.md#debug-symbols) for
backend behavior and current limitations.

Set each directory in its profile's `build_dir` field. Defaults are
`Clutch-build-debug` and `Clutch-build-release`; there is no build-directory CLI
override. Relative and absolute paths are accepted. Use distinct paths to keep
configurations independent.

Configuration writes native build files, `configuration.toml`, tool selections,
`build-graph.dot`, and `compile_commands.json` inside the selected directory. The database includes
absolute per-source arguments and build-directory frames for application and
test units. Point the IDE or language server at that directory. Clutch creates
no database copy or symlink in the project root and leaves editor settings to
the editor or its extensions. IDE and developer-tool clients can use the [file API](file-api.md) to
request the resolved project, target and toolchain model. `clutch capabilities`
and `clutch locate --project <source> [--release]` provide JSON discovery without
configuring or compiling a project.

Use the selected profile's compilation database for code completion:

- **clangd:** set `--compile-commands-dir` to the build directory, or select it in
  your own `.clangd` configuration. See the
  [clangd configuration reference](https://clangd.llvm.org/config#compilationdatabase).
- **VS Code Microsoft C/C++:** set `compileCommands` to the generated database in
  the extension's configuration. See the
  [C/C++ settings reference](https://code.visualstudio.com/docs/cpp/customize-cpp-settings).
- **CLion:** open `compile_commands.json` as a project, select the matching
  toolchain and enable database auto-reload. See the
  [compilation database guide](https://www.jetbrains.com/help/clion/compilation-database.html).
- **Visual Studio and Xcode:** open the native solution/project emitted by the
  selected generator.

A workspace has one database covering all members and their individual standards
and compile requirements. Select the root profile's database and switch databases
when changing profiles. Configure and run the language service in the same host
or container environment: the absolute compiler/source paths must exist there.
Selecting a container's database on the host does not translate its paths.

Existing IDEs require an adapter to consume Clutch's file API directly; CMake
extensions cannot consume its schemas unchanged. IDE extension implementations
are maintained separately from Clutch's native core.

Inspect compile/link dependencies with Graphviz:

```sh
dot -Tsvg Clutch-build-debug/build-graph.dot -o Clutch-build-debug/build-graph.svg
```

The [development container](development-container.md) includes Graphviz; Clutch
does not require it to configure or build. Clutch validates cycles and duplicate artifact
producers independently of rendering. Compiler-discovered headers and future
package dependencies are outside this visualization.

Objects, executables, and backend metadata also stay in the profile directory.
The reserved `.clutch/` directory is the project-local dependency workspace for
future package management; it is excluded from Git and Docker build contexts.
Existing nonempty directories must already belong to this project; Clutch
refuses unrelated or redirected paths. Saved configuration is local to its
project location and profile. Configure again after moving a project.

## Project workflows

```sh
clutch new hello-c --language c
clutch init . --name existing_project
clutch test --project hello
clutch test --project hello --filter arithmetic
clutch clean --project hello
clutch clean --project hello --release
```

`new` requires a new directory. `init` preserves existing sources and refuses
to replace a manifest. Configure before the first build. `run` builds the
application, forwards arguments, and returns its exit code. `clean` removes the
selected profile's owned directory, including its saved configuration and IDE
database. Reconfigure after cleaning.

Declare independent executable tests, each supplying its own `main`:

```toml
[[test]]
name = "arithmetic"
sources = ["tests/arithmetic.cpp", "src/arithmetic.cpp"]
```

Tests inherit standards, includes, and definitions, and may use either language
independently of the application's declaration. Application sources are not
automatically linked into tests. Reconfigure after changing declarations.
`test` builds selected tests, continues after runtime failures, and fails if any
test fails. An unmatched explicit filter is an error. Ordinary `build` compiles
ordinary default targets. `NO_COLOR` disables status colors.

Use `clutch targets` to list configured target names and kinds. `build`, `run`
and `test` accept `--target <name>` (short form `-t`). Repeat it for multiple
build/test targets. Without selection, `build` uses ordinary targets marked
`default`, while `test` uses all tests. `run` requires one application executable.
