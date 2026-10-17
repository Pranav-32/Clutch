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

The [complete `clutch.toml` reference](manifest.md) lists every supported
field, type, default, accepted value and compiler-flag mapping, with complete
examples for standards, profiles, sources and tests.

| Table | Supported fields |
| --- | --- |
| `[project]` | `name`, `version`, `language`, `std`, `entry` |
| `[build]` | `generator`, `sources`, `include_dirs`, `defines` |
| `[profile.debug]`, `[profile.release]` | `build_dir`, `optimize`, `warnings` |
| `[[test]]` | `name`, `sources` |

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
`project.entry` can add an entry source. All application sources currently form
one executable, and `version` is metadata.

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

## Build directories and IDEs

Set each directory in its profile's `build_dir` field. Defaults are
`Clutch-build-debug` and `Clutch-build-release`; there is no build-directory CLI
override. Relative and absolute paths are accepted. Use distinct paths to keep
configurations independent.

Configuration writes native build files, `configuration.toml`, tool selections,
`build-graph.dot`, and `compile_commands.json` inside the selected directory. The database includes
absolute per-source arguments and build-directory frames for application and
test units. Point the IDE or language server at that directory. Clutch creates
no database copy or symlink in the project root.

Inspect compile/link dependencies with Graphviz:

```sh
dot -Tsvg Clutch-build-debug/build-graph.dot -o Clutch-build-debug/build-graph.svg
```

The development container includes Graphviz; Clutch does not require it to
configure or build. The [container guide](development-container.md#inspect-a-projects-build-graph)
explains graph inspection. Clutch validates cycles and duplicate artifact
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
only the application. `NO_COLOR` disables status colors.
