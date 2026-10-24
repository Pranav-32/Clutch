# Using Clutch

For an existing `CMakeLists.txt` application, see
[CMake project workflows](cmake-projects.md). The commands below describe
projects configured from native `clutch.toml` manifests.

Audience: Users configuring, building and testing C/C++ projects with Clutch.

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
files without building project artifacts. Optional safety checks compile/link
small probes; external-library configuration may also run compiler probes or
upstream configure scripts. See [profile safety](manifest.md#safety) and
[dependencies](dependencies.md#cmake-libraries).
`build`, `run`, and `test` load the saved configuration
and execute the selected native backend. They require configuration first.
Changes to `clutch.toml` require another explicit `clutch .` invocation; adding
or removing discovered sources also requires reconfiguration. Editing an
existing source or header only requires a build.

To force recompilation with the saved settings, add `--clean`:

```sh
clutch build --clean
clutch run --clean --target my_app
clutch test --clean --jobs 4
```

The selected targets and their build dependencies are rebuilt, including native
Clutch libraries, precompiled headers and CMake/Make dependencies. Other profiles,
downloaded sources, lock files and IDE query/reply metadata are retained.
`run` starts the newly built executable only after the build succeeds; `test`
likewise runs tests only after a successful build. The separate `clutch clean`
command removes the owned profile directory, requiring configuration afterward.

Source arrays accept patterns such as `sources = ["src/**.{cpp,c,h}"]`.
`*.cpp` selects files directly beside the manifest; `**.cpp` selects recursively.
Omitting both sources and entry discovers language-appropriate C/C++ files and
headers recursively from that manifest, stopping at nested Clutch manifests.
Use the same syntax for library and test targets. Matched headers are IDE files;
only C/C++ source extensions are compiled. See [source selection](manifest.md#sources-and-headers)
for matching rules and generated-directory exclusions.

Configuration creates or updates `clutch.lock` at the source/workspace root.
Commit it with the manifests. Use `clutch . --locked` (or add `--release`) to
require an existing matching lock without rewriting resolution. This flag is
configuration-only. Changes to any root/member manifest or the lock invalidate
saved builds; see [lock files](lock-files.md) for local edges and Git revision pins.

Native builds use available CPU concurrency by default; `--jobs N` sets an
explicit limit and `--jobs 1` requests serial execution. Ninja, GNU Make, JOM,
MSBuild and Xcode own dependency scheduling. NMake remains serial because its
executor has no parallel scheduler. Exact compiler/link prerequisites prevent a
consumer from running before its library or PCH is ready.

Git imports are downloaded during configuration into the source root's `.clutch/`.
Use `clutch . --locked --offline` after caching them to prohibit network access.
Refresh untagged imports explicitly with `clutch . --auto-update-dependencies`;
normal configuration preserves compatible pins. See [dependencies](dependencies.md)
and [PCH settings](manifest.md#precompiled-headers).

Use `clutch outdated` to check compatible dependency updates without applying them,
or `clutch . --check-updates` to report them after configuration. These checks may
fetch sources to inspect transitive manifests; they do not compile libraries or
replace existing pins. Applying updates remains explicit.

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

When neither generator nor generic build-tool override is supplied, Clutch first
tries the profile's cached generator, then Ninja, GNU Make, NMake, JOM, Visual
Studio and Xcode, skipping unsupported hosts, absent/old executors and incompatible
compiler choices. Ninja requires 1.10+, GNU Make requires 4+, Visual Studio needs
MSVC-style drivers, and Xcode needs Apple Clang and an active full installation.
The selected pair is saved for subsequent builds. Explicit generator/tool choices
fail if unusable rather than silently changing formats. Without a generator,
`--build-tool` or `CLUTCH_BUILD_TOOL` can identify a standard executor by name
(for example `ninja`, `gmake`, `nmake`, `MSBuild.exe`); custom wrapper names require
`--generator` too. A stale missing cached executable is rediscovered.

On Windows, Ninja discovery also checks the active Visual Studio installation,
bundles beside CMake on `PATH`, and standard Visual Studio/CLion installation
directories under Program Files. The resolved executable is saved in the
selected profile's build directory. A fresh build directory runs discovery
again; use `--build-tool` for a custom installation that is not discoverable.

Resolved settings and the complete graph are saved inside the profile build
directory. Build execution accepts `--project`, `--release`, `--jobs` and `--clean`; change
compilers, generator, or build-tool paths by configuring again. `run` additionally
forwards program arguments, and `test --filter text` selects tests.

Generated native builds also prepare imported CMake/Make libraries and their
generated headers. Windows builds stage imported runtime DLLs even when the
executable is already current. Keep the configured Clutch executable available
for these saved callbacks. `clutch build --clean` applies the dependency adapters'
forced-build actions as well as rebuilding selected native targets; see
[dependency adapters](dependencies.md#contributing-a-foreign-adapter) for scheduling
and NMake/JOM behavior.

## Format, analyze and debug

These native-manifest commands run optional tools installed on your machine:

```sh
clutch fmt --check                  # Report differences without editing
clutch fmt                          # Apply clang-format
clutch .                            # Produce the compilation database
clutch lint                         # Run clang-tidy using that database
clutch lint --checks=-*,bugprone-*
clutch debug --target my_app -- "argument with spaces"
clutch debug --release --target my_app
```

`fmt` reads the manifest without configuring, fetching dependencies or compiling.
It formats selected project/workspace sources, selected headers and sibling
headers in those source directories. Add headers in other directories to the
source patterns when you want them included. It follows `.clang-format` discovery
and does not traverse dependency caches or unrelated repository directories.

`lint` requires a current configuration, reads `.clang-tidy`, and processes the
project/workspace translation units using the profile's compilation database.
Dependency targets are excluded; included headers can still produce diagnostics
according to the analyzer configuration. It stops on a failing tool invocation
and preserves that exit status. It does not apply automatic source fixes.
Build dependency-generated headers first if the project needs them for analysis.

`debug` builds the selected executable using the same target selection, `--jobs`
and `--clean` options as `run`, then opens an interactive LLDB, GDB or CDB session
in that order of availability. Use `--tool <executable>` to select a debugger,
formatter or analyzer explicitly. Debuggers must retain their supported executable
name (`lldb`, `gdb`, versioned variants or `cdb`), even when given by absolute path.
Program arguments go after `--`. The selected profile must have
`debug_symbols = true`; release profiles can enable this without changing
optimization. The native debugger owns stepping, breakpoints and startup behavior.

In CLion, add `clutch fmt --check` or `clutch lint` as an External Tool with the
project directory as its working directory. Run interactive `clutch debug` from
the IDE terminal. These commands do not install a CLion plugin or embed the CLI
debugger into CLion's debugger UI. IDE integration can use the
[File API](file-api.md) executable paths, working directories, exact compiler
arguments and debug actions. A dedicated extension can consume that API separately.

All three commands accept `--project <directory>` and `--release`; they add no
formatter, analyzer, debugger or Python runtime requirement to ordinary Clutch
builds. See [profile safety](manifest.md#safety) for opt-in compile diagnostics and
runtime sanitizers, and the [tooling example](../examples/tooling/README.md).

## Project commands

Use `[[command]]` declarations for scripts or other executables. Run a named
command with `clutch exec <name> -- <extra arguments>`, even before the project
has sources or a saved configuration. An optional `phases` array attaches it to
`before-configure`, `after-configure`, `before-build` or `after-build`.

Commands use separate argv elements, inherit the environment and stop the
operation if they fail. They do not require an embedded interpreter. Build hooks
wrap `clutch build`, `run`, `test` and `debug`, including no-op builds; direct
native executor invocations do not run them. See the [field reference](manifest.md#commands-and-lifecycle-hooks)
and [runnable example](../examples/commands/README.md) for ordering, context
tokens, workspace ownership and generated-file limitations.

## Watch for changes

For a native `clutch.toml` project, leave Clutch running while you edit:

```sh
clutch watch
clutch watch --test
clutch watch --run --target my_app -- "argument with spaces"
clutch watch --project my_project --release --jobs 4
clutch watch --path assets --path scripts --ignore assets/generated
```

Watch performs an initial build, configuring first if saved settings are absent
or stale. It reuses cached compiler/generator selections. For explicit tool
choices, run `clutch . --cc ... --cxx ... --generator ...` before starting watch.
Each cycle uses the normal build/run/test workflow, including lifecycle hooks,
target selection and runtime staging. Failed configuration, compilation, hooks
or application/tests do not end the watch session; another edit starts a new
attempt. A source, header or watched-file addition/removal triggers configuration
again. Content edits to a manifest, lock, version/template input or recognized
CMake/Make rule also reconfigure; ordinary source/header edits only rebuild.

| Option | Default | Meaning |
| --- | --- | --- |
| `--run` | Off | Build and run a short-lived application after each change; program arguments follow `--`. |
| `--test` | Off | Build and run selected test executables. Cannot be combined with `--run`. |
| `--target`, `-t` | Normal target selection | Repeatable target names, including qualified workspace names. |
| `--project`, `--release`, `--jobs` | Normal project/profile/job defaults | Select the native root, configured profile and compiler concurrency. |
| `--interval` | `250` | Poll interval in milliseconds, from 1 to 60000. |
| `--debounce` | `200` | Quiet interval in milliseconds after the last observed edit, from 0 to 60000. A burst of saves becomes one cycle. |
| `--path` | Empty | Extra file/directory to observe; repeatable, relative to the project or absolute. Explicit directories include all regular files. |
| `--ignore` | Empty | Exclude a file or directory subtree; repeatable, relative to the project or absolute. Uses paths, not glob expressions. |

Default coverage includes C/C++ sources and headers beneath the root, workspace
members and local path dependencies, plus saved configuration inputs and selected
additional files. Unrelated nested Clutch projects are skipped. Add `--path` for
scripts, resources, include trees outside these roots or other inputs. Installed
SDKs are not recursively scanned automatically. Output directories for both
profiles, recognized Clutch/CMake build trees, `.clutch` caches, `.git`, ignored
paths, symlinks and redirected directories are excluded. Explicit paths do not
override those output/cache exclusions.

Polling compares content signatures, so touching unchanged files does not rebuild.
When changed content retains or moves back its timestamp, the watcher requests
`--clean` for the next build so a timestamp-based executor cannot reuse old
objects. Hashing cost grows with watched bytes; increase `--interval` for large
trees. Traversal is bounded to 256 source roots and 100000 inspected entries per
snapshot. Missing/unreadable inputs remain observable states; directory scans
skip inaccessible entries instead of following redirected paths.

Cycles never overlap. Edits made during a build are checked afterward and queued
for another debounced cycle. `--run` waits for the program to exit; it does not
restart a long-running server. Ctrl+C requests shutdown; an active child must
finish or handle the signal before the parent returns. Watch does not force-kill
running tools. Keep generated output in the build directory or exclude it with
`--ignore` to avoid a hook repeatedly changing its own watched inputs.

The watcher is native C++ with no Python, Node or platform-watcher dependency.
It currently covers native manifests; whole-project CMake provider watching is
not implemented. It adds no manifest fields and does not update Git dependencies
to newer revisions implicitly.

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
| `[build]` | `generator`, `sources`, `links`, `include_dirs`, `defines`, `pch`, `runtime` |
| `[profile.debug]`, `[profile.release]` | `build_dir`, `optimize`, `warnings`, `debug_symbols`, `safety` |
| `[[command]]` | `name`, `argv`, `working_dir`, `phases` |
| `[[target]]` | `name`, `type`, `sources`, `include_dirs`, `public_include_dirs`, `defines`, `public_defines`, `links`, `public_links`, `default`, `pch` |
| `[[test]]` | Target fields except `default`; executable only |
| `[dependencies]` | Aliased Git, local, installed-package or catalog imports; see [dependency fields](dependencies.md#dependency-field-reference) |

```toml
[project]
name = "hello"
version = "0.1.0"
language = "cpp"        # c, cpp (or c++), mixed, auto
std = 20                # Integer or quoted language-standard version

[build]
generator = "ninja"     # Native build-file format/backend
# Optional: without sources or entry, discover recursively from this manifest.
sources = ["src/main.cpp"]
include_dirs = ["headers"]
defines = ["APP_VERSION=1"]

[profile.debug]
build_dir = "Clutch-build-debug"

[profile.release]
build_dir = "Clutch-build-release"
optimize = "speed"       # none, speed, size
warnings = "extra"       # Preset, or an array such as ["extra", "shadow"]
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

Application shorthand can coexist with library targets: keep `project.entry` or
`build.sources`, then use `build.links` for the application's libraries. Workspace
roots may share include directories and definitions with members, which can
override them locally. The [flexible example](../examples/flexible/README.md)
combines these forms in one runnable project.

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
does not build project artifacts; optional safety and external-adapter probes may
compile small checks. The next build recreates invalidated project outputs.

## Build directories and IDEs

Profiles control symbols through `debug_symbols`: debug defaults to `true`,
release to `false`, independently of optimization. MSVC-style executable/DLL links
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
producers independently of rendering. The graph includes logical package edges
and PCH prerequisites; compiler-discovered headers are outside this visualization.

Objects, executables, and backend metadata also stay in the profile directory.
The reserved source-root `.clutch/` directory contains the project-local Git cache;
it is excluded from Git and Docker build contexts.
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

Use `clutch . --targets` to discover project and CMake dependency library targets
before selecting imports. Discovery may fetch sources and run native configuration;
it leaves the saved Clutch graph and lock unchanged. Use `clutch targets` to list
configured target names and kinds. `build`, `run`
and `test` accept `--target <name>` (short form `-t`). Repeat it for multiple
build/test targets. Without selection, `build` uses ordinary targets marked
`default`, while `test` uses all tests. `run` requires one application executable.

## Source bundles

`clutch bundle` creates a source ZIP containing the project, workspace members
and snapshots of resolved Git/local source dependencies. It runs natively;
neither Python nor an external ZIP utility is required.

```sh
clutch .
clutch bundle
clutch bundle --output ../hello-sources.zip --exclude private --exclude scratch
```

The default output is `.clutch/bundles/<project>-sources.zip`. `--project` selects
another project directory; `--release` selects its release-profile inputs.
An explicit output path is relative to the current working directory. Existing
outputs are refused. `--exclude` accepts repeatable archive-relative file or
directory paths, without wildcards.

Configure first when the project imports Git/catalog dependencies: bundling
uses existing pins and verified cached checkouts offline. It does not update
versions, fetch missing source revisions, compile code or replace the original
manifest/lock. Projects using only local files can bundle without configuring.

After extracting the ZIP, configure for the recipient's machine before building:

```sh
clutch . --offline
clutch build
clutch test
```

Supply `--cc`, `--cxx` or `--generator` during configuration when needed.
Source imports become relative `path` snapshots, including transitive imports.
External snapshots live in `.clutch/bundle/deps/`; package origins/revisions
are recorded in `.clutch/bundle/snapshot.toml`. The new configuration generates
a fresh `clutch.lock` and native rules. Profile build directories use the default
`Clutch-build-debug` / `Clutch-build-release` names in the snapshot.

The archive includes regular files, empty directories and available executable
permissions. It omits repository metadata, existing `.clutch` caches, lock/writer
files and recognized Clutch/CMake build trees. Other files are included regardless
of `.gitignore`; use exclusions for material you do not intend to distribute.
Required manifests and selected sources cannot be excluded. Symlinks, junctions,
special files, unsafe path components and ASCII case-colliding filenames are
rejected. Unicode normalization/case rules still depend on the recipient filesystem.

Compilers, build tools and installed SDKs remain prerequisites. A machine-specific
SDK prefix outside the bundled trees is rejected; use environment-based package
discovery or a redistributable prefix inside the project. Foreign build scripts,
literal arguments and embedded absolute paths must themselves be portable;
the bundler rewrites known Clutch manifest path fields, not arbitrary scripts.
Foreign download commands may require separate offline settings. Review included
licenses before redistributing dependencies, and avoid editing inputs while bundling.

Archives use standard uncompressed ZIP records with CRC-32 checks, sorted members
and fixed timestamps. The same unchanged source snapshot on the same host produces
the same bytes. ZIP64 is not supported: archives must stay below four GiB and
contain at most 65,534 entries, including directories and generated metadata.
