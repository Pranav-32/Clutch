# clutch.toml reference

`clutch.toml` describes one C/C++ application executable and optional independent
test executables. This reference covers the fields currently implemented by
Clutch. Configure the directory containing the manifest before building:

```sh
clutch .
clutch build
clutch test
```

Configuration validates the manifest, resolves tools and sources, and writes
native build files, saved configuration, `compile_commands.json` and
`build-graph.dot` in the selected profile's build directory. It does not compile.
Run `clutch .` again after changing the manifest or adding/removing sources.
Existing source/header edits only need another build.

Try the [runnable examples](examples.md) for complete single-file, multi-file,
C and mixed-language projects using these fields.

## Tables and value rules

| Table | Required | Purpose |
| --- | --- | --- |
| `[project]` | Yes | Project name/version, language, standards and optional entry source |
| `[build]` | No | Generator, application sources, include directories and definitions |
| `[profile.debug]` | No | Debug build directory, optimization and warnings |
| `[profile.release]` | No | Release build directory, optimization and warnings |
| `[[test]]` | No; repeatable | Independent executable test name and sources |

Unknown keys and unsupported profile names are errors. Values use the types
listed below; strings such as language/profile policy names use the documented
lowercase spellings. Generator names are normalized separately. `[dependencies]`
is reserved but rejected, including an empty table, until dependency management
is implemented.

Relative paths resolve from the directory containing `clutch.toml`, regardless
of the command's working directory. Absolute paths are accepted. Input paths must
exist during configuration. Paths are literal: shell variables, `~`, wildcards
and command substitutions are not expanded.

Forward slashes work in Windows TOML paths too. For backslashes, use TOML literal
strings (`'D:\code\headers'`) or escape each backslash in a basic quoted string
(`"D:\\code\\headers"`).

## [project]

| Field | Type | Required/default | Behavior |
| --- | --- | --- | --- |
| `name` | String | Required | Nonempty name containing letters, digits, `_` or `-`. Names the application executable, with `.exe` appended on Windows. |
| `version` | String | Required | Nonempty project metadata. Currently not checked as semantic versioning, added as a macro, or used for dependency resolution. |
| `language` | String | `"auto"` | `"c"`, `"cpp"`, `"c++"`, `"mixed"` or `"auto"`; `"c++"` is an alias for `"cpp"`. |
| `std` | Integer, string or table | C `11`; C++ `17` | Select language standards; see the rules below. |
| `entry` | String path | Omitted | Add one application source to explicit/discovered sources. This is additive, not a replacement for discovery. |

Example for C++:

```toml
[project]
name = "hello"
version = "0.1.0"
language = "cpp"
std = 17
```

`language = "auto"` inspects application sources: C-only inputs select C,
C++-only inputs select C++, and both select mixed. Explicit `"c"` and `"cpp"`
must match application source types. Tests may use either language independently.
`"mixed"` permits both languages, including a project whose current application
sources happen to use only one.

### Language standards

| Language | Accepted versions | GCC/Clang-style flag | Microsoft-style flag |
| --- | --- | --- | --- |
| C | `99`, `11`, `17`, `23` | `-std=c99`, `-std=c11`, `-std=c17`, `-std=c23` | C11/C17 only: `/std:c11`, `/std:c17` |
| C++ | `11`, `14`, `17`, `20`, `23` | `-std=c++11` through `-std=c++23` | C++14/17/20: `/std:c++14`, `/std:c++17`, `/std:c++20`; C++23 selects `/std:c++latest` |

The selected compiler and standard library must support the requested mode.
Microsoft-style drivers reject C++11 here. Clang using Microsoft's standard
library also requires a compatible language mode; use C++14 or newer for that
combination. `latest` on Microsoft-style drivers is an evolving mode, not a
guarantee of exact C++23 conformance.

For a single-language application, an integer or string applies to that language:

```toml
# Under [project] in a C project:
language = "c"
std = "17"
```

A mixed-language application's `std` must be a table when specified:

```toml
[project]
name = "mixed_app"
version = "0.1.0"
language = "mixed"
std = { c = 11, cpp = 20 }
```

The table form is also accepted for single-language applications, for example
when a C application has C++ tests. Either subfield may be omitted, retaining
that language's default. Only `c` and `cpp` are valid subkeys; each accepts an
integer or string standard version. Omitting `std` entirely retains C11/C++17.
Clutch validates both standard values even if a language is currently unused.

## [build]

| Field | Type | Default | Behavior |
| --- | --- | --- | --- |
| `generator` | String | `"ninja"` | Native build-file format and executor family; the configuration CLI's `--generator` overrides it. |
| `sources` | Array of string paths | Discover recursively under `src/` | Application compilation units; a nonempty array replaces discovery. An empty array also triggers discovery. |
| `include_dirs` | Array of string paths | Empty, plus an existing root `include/` | Existing directories added to every application's and test's header search paths. |
| `defines` | Array of strings | Empty | Preprocessor definitions added to every application/test unit. |

```toml
[build]
generator = "ninja"
sources = ["src/main.cpp", "src/math.cpp", "src/platform.c"]
include_dirs = ["src", "headers", "third_party/example"]
defines = ["APP_VERSION=1", "ENABLE_LOGGING"]
```

### Sources and headers

Supported source extensions are `.c`, `.cc`, `.cpp` and `.cxx`, with these exact
lowercase spellings. `.c` uses the C compiler; the other extensions use C++.
Source paths must identify existing regular files. Directories, globs and header
files are not accepted as compilation units. Clutch sorts and deduplicates the
combined explicit/discovered sources and optional `project.entry`.

All application sources form one executable, so they must collectively provide
one entry point. Discovery includes nested source directories; place independent
test programs outside `src/` or select application sources explicitly. At least
one application source is required, even when tests are declared.

For a single file outside `src/`, either specify `sources = ["main.cpp"]` under
`[build]` or `entry = "main.cpp"` under `[project]`. `entry` alone still includes
any sources found under `src/`.

Header directories can be colocated with sources. An existing root `include/`
directory is automatically added, but that directory layout is optional. Headers
do not need to be listed as sources. Include paths become `-I<path>` or
`/I<path>`; definitions become `-D<value>` or `/D<value>`. Supply only the path or
definition, without these flag prefixes. Each definition is one argument; an
equals sign gives it a value, while a bare name defines the macro without an
explicit value in the command line.

### Generators and compiler selection

| `generator` | Native output | Requirements |
| --- | --- | --- |
| `"ninja"` | `build.ninja` | Ninja 1.10+ and a compatible host compiler |
| `"make"` | GNU `Makefile` | GNU Make, including compatible MinGW/MSYS variants |
| `"nmake"` | NMake `Makefile` | Windows, NMake and initialized Microsoft compiler/SDK environment |
| `"jom"` | JOM `Makefile` | Windows, JOM and initialized Microsoft compiler/SDK environment |
| `"visual-studio"` | `clutch.sln`, native `.vcxproj` files, `clutch.proj` | Windows, MSBuild C++ tools and MSVC `cl` |
| `"xcode"` | `<name>.xcodeproj` and shared schemes | macOS, full Xcode and Apple's Clang |

`clutch generators` lists generators and current-host availability. Accepted
aliases include `"Unix Makefiles"`, `"MinGW Makefiles"`, `"MSYS Makefiles"`,
`"GNU Make"`, `"gmake"`, `"NMake Makefiles"`, `"NMake Makefiles JOM"`,
`"Visual Studio"`, `"vs"` and `"msbuild"`. Canonical names above are recommended
for portable manifests. Generator selection does not install the executor or
compiler, and IDE projects describe only the selected profile.

There are currently no manifest `cc`, `cxx`, `compiler`, `toolchain`,
`build_tool`, `cflags`, `cxxflags` or `ldflags` fields. Select installed compiler
executables and the build executor during configuration:

```sh
clutch . --generator ninja --cc clang --cxx clang++ --build-tool ninja
```

Compiler selection precedence is `--cc`/`--cxx`, then `CC`/`CXX`, saved profile
selections, then discovery. Build executor precedence is `--build-tool`,
`CLUTCH_BUILD_TOOL`, the generator's environment variable, saved selection, then
discovery. Generator variables are `NINJA`, `MAKE`, `NMAKE`, `JOM`, `MSBUILD` and
`XCODEBUILD`. These values are executable names/paths, not commands with flags.
Reconfigure to change selections; `build`, `run` and `test` use saved settings.

## [profile.debug] and [profile.release]

Both profiles accept the same three string fields. Debug is selected by default;
`--release` selects release during configuration and execution. Profiles apply
their own defaults independently and do not inherit overrides from each other.
Custom profiles and per-test profile overrides are not implemented.

| Field | Debug default | Release default | Meaning |
| --- | --- | --- | --- |
| `build_dir` | `"Clutch-build-debug"` | `"Clutch-build-release"` | Nonempty relative/absolute output directory for native rules, caches, IDE metadata, objects and executable outputs |
| `optimize` | `"none"` | `"speed"` | `"none"`, `"speed"` or `"size"`; mapping below |
| `warnings` | `"extra"` | `"extra"` | `"none"`, `"default"` or `"extra"`; mapping below |

```toml
[profile.debug]
build_dir = "output/debug"
optimize = "none"
warnings = "extra"

[profile.release]
build_dir = "output/release"
optimize = "speed"
warnings = "none"
```

| Policy | GCC/Clang-style flags | Microsoft-style flags |
| --- | --- | --- |
| `optimize = "none"` | `-O0` | `/Od` |
| `optimize = "speed"` | `-O3` | `/O2` |
| `optimize = "size"` | `-Os` | `/O1` |
| `warnings = "none"` | `-w` | `/W0` |
| `warnings = "default"` | `-Wall` | `/W3` |
| `warnings = "extra"` | `-Wall -Wextra` | `/W4` |

`warnings = "none"` suppresses compiler warnings; errors still fail the build.
Debug builds also enable debugging information (`-g` or `/Z7`, plus the applicable
linker debug settings). Optimization and warning overrides do not change which
profile is active. Native IDE generators express corresponding settings through
their project formats while preserving Clutch's selected policies.

Use distinct directories for debug/release and for different host/compiler
setups. There is no command-line build-directory override. Clutch marks owned
directories and refuses unrelated nonempty directories, redirected paths,
reserved source/metadata locations, or a build directory that contains an input
source file or include directory. `clutch clean` removes only the selected owned profile.
Reconfigure after cleaning or moving the project.

## [[test]]

Repeat `[[test]]` once per independent executable test:

| Field | Type | Required/default | Behavior |
| --- | --- | --- | --- |
| `name` | String | Required | Nonempty letters/digits/`_`/`-`; must be unique among tests |
| `sources` | Nonempty array of string paths | Required | Complete set of existing C/C++ compilation units for this test |

```toml
[[test]]
name = "math"
sources = ["tests/math.cpp", "src/math.cpp"]

[[test]]
name = "c_api"
sources = ["tests/c_api.c", "src/api.c"]
```

Each test provides its own `main` and inherits both language standards, include
paths, definitions and the active profile's policies. Application sources are
not linked automatically; list any shared implementation sources explicitly.
Test sources are deduplicated per target and may use either or both languages.

`clutch build` builds only the application. `clutch test` builds and runs all
declared tests; `clutch test --filter math` selects test names containing `math`
(a case-sensitive substring). A filter matching nothing is an error. Without a
filter, a project with no tests reports that none are declared and succeeds.
After a successful test build, execution continues across runtime failures and
returns failure if any selected test fails. Test executables live under the
profile directory's `tests/` subdirectory and run with the source root as cwd.

## Complete example

This layout keeps the application's entry point separate from executable tests:

```text
hello/
  clutch.toml
  src/
    main.cpp
    math.cpp
    math.hpp
  tests/
    math.cpp
```

```toml
[project]
name = "hello"
version = "0.1.0"
language = "cpp"
std = 17

[build]
generator = "ninja"
sources = ["src/main.cpp", "src/math.cpp"]
include_dirs = ["src"]
defines = ["APP_VERSION=1"]

[profile.debug]
build_dir = "Clutch-build-debug"
optimize = "none"
warnings = "extra"

[profile.release]
build_dir = "Clutch-build-release"
optimize = "speed"
warnings = "extra"

[[test]]
name = "math"
sources = ["tests/math.cpp", "src/math.cpp"]
```

```sh
clutch .
clutch run
clutch test --filter math
clutch . --release
clutch build --release
```

## Migration and unsupported features

| Old spelling | Current form |
| --- | --- |
| `[package]` | `[project]` |
| `project.edition` | `project.std` |
| `project.c_standard` | `project.std.c` in a standards table |
| `build.toolchain` for Ninja/Make/IDE selection | `build.generator` |

Old spellings receive migration diagnostics. Regenerate configuration with
`clutch .` after migrating. TOML structural/type errors and unknown fields also
fail configuration rather than silently enabling a different setting.

Git dependency imports, foreign CMake/Make/Blaze adapters, folder aliases,
automatic dependency updates, library targets, custom build hooks, PCH, bundled
archives and general cross-compilation configuration remain future work. Do not
add fields for these to an active manifest yet. Clutch's own configuration
engine and supported generators operate independently of the CMake project used
to develop Clutch itself.
