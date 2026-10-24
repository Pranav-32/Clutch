# clutch.toml reference

Audience: Contributors changing manifest behavior, and users looking up fields.
For a first contribution, start with [Contributing](CONTRIBUTING.md). For a small
working manifest, jump to the [complete example](#complete-example).

`clutch.toml` describes C/C++ executable and library targets and optional
test executables. This reference covers the fields currently implemented by
Clutch. Configure the directory containing the manifest before building:

```sh
clutch .
clutch build
clutch test
```

Configuration validates the manifest, resolves tools and sources, and writes
native build files, saved configuration, `compile_commands.json` and
`build-graph.dot` in the selected profile's build directory. Safety settings and
external adapters may run compiler/configuration probes; project artifacts are
built by build/run/test.
Run `clutch .` again after changing the manifest or adding/removing sources.
Existing source/header edits only need another build.

Use [clutch watch](usage.md#watch-for-changes) to repeat this workflow as files
change. Its CLI options control debounce, extra paths and exclusions; there is
no watch table in the manifest.

Point your IDE at the selected profile's compilation database. See
[using Clutch](usage.md#build-directories-and-ides) for setup. The [file API](file-api.md)
uses client query files in the profile directory; no extra manifest fields are
required.

Try the [examples by use case](../examples/README.md#find-a-scenario) for runnable
manifests and commands. Start with a small project, then add one feature at a time:

| Feature | Example |
| --- | --- |
| Source discovery and language-specific patterns | [Discovery](../examples/discovery/README.md) |
| Static/shared targets and workspaces | [Targets](../examples/targets/README.md), [shared library](../examples/shared-library/README.md), [workspace](../examples/workspace/README.md) |
| Dependencies and foreign build systems | [Dependency collection](../examples/dependencies/README.md), [CMake projects](../examples/cmake-project/README.md) |
| Optimization, warnings, symbols and safety | [Profiles](../examples/profiles/README.md), [safety variants](../examples/safety/README.md) |
| Generated files and lifecycle commands | [Configured files](../examples/configured-files/README.md), [commands](../examples/commands/README.md) |
| Edit/build cycles and portable source archives | [Watch](../examples/watch/README.md), [bundle](../examples/bundle/README.md) |
| IDE integration and native build files | [File API](../examples/file-api/README.md), [generators](../examples/generators/README.md) |

## Tables and value rules

| Table | Required | Purpose |
| --- | --- | --- |
| `[project]` | Yes, except a virtual workspace root | Project name/version, language, standards and optional entry source |
| `[build]` | No | Generator, application sources/links, include directories and definitions |
| `[profile.debug]` | No | Debug build directory, optimization, warnings, symbols and safety checks |
| `[profile.release]` | No | Release build directory, optimization, warnings, symbols and safety checks |
| `[workspace]` | No | Compose explicit member projects; a virtual root may omit `[project]` |
| `[[target]]` | No; repeatable | Explicit executable, static or shared library |
| `[[test]]` | No; repeatable | Independent executable test name and sources |
| `[[configure_file]]` | No; repeatable | Generate project-private headers/text from literal templates |
| `[[command]]` | No; repeatable | Named commands and optional configure/build lifecycle hooks |
| `[dependencies]` | No | Git, local, installed SDK and supported named source-catalog imports |

Unknown keys and unsupported profile names are errors. Values use the types
listed below; strings such as language/profile policy names use the documented
lowercase spellings. Generator names are normalized separately.

Relative paths resolve from the directory containing `clutch.toml`, regardless
of the command's working directory. Absolute paths are accepted. Project input
paths must exist during configuration. Only [source selections](#sources-and-headers)
expand supported file patterns. Other paths are literal: shell variables, `~`
and command substitutions are not expanded.

Forward slashes work in Windows TOML paths too. For backslashes, use TOML literal
strings (`'D:\code\headers'`) or escape each backslash in a basic quoted string
(`"D:\\code\\headers"`).

## [workspace]

| Field | Type | Required/default | Behavior |
| --- | --- | --- | --- |
| `members` | Nonempty array of string directory paths | Required when workspace is present | Explicit physical member directories inside the root; each contains its own `clutch.toml` |

Workspace roots coordinate profiles and generator selection. A root with no
`[project]` is virtual and declares no sources/targets/tests of its own; its
`[build]` accepts `generator`, shared `include_dirs`, shared `defines`, shared `pch`
and `runtime`. A root with `[project]` participates as a real project and may
also declare application sources and links. Root include directories and
definitions are shared with members in either layout. Members retain their own
language/standards and source-relative paths,
but cannot override `[profile]` or `build.generator`. Unknown workspace fields,
duplicate directories/project names, escaping/redirected paths and nested
workspaces are rejected. Globs/exclusions and automatic upward discovery are
future work. See [workspaces](workspaces.md) for a complete example.

Targets use `<project>::<target>` identities throughout the workspace. Local
unqualified links resolve within their member; cross-member links and CLI target
selection use qualified names. Each member's target artifacts live under
`<profile-build-dir>/members/<project>/`.

Configuration also generates [clutch.lock](lock-files.md). It is separate from
manifest fields and host-specific `backend.toml`/`configuration.toml` caches.

## [project]

| Field | Type | Required/default | Behavior |
| --- | --- | --- | --- |
| `name` | String | Required | Nonempty name containing letters, digits, `_` or `-`. Also names the default application created from project source settings, with `.exe` appended on Windows. |
| `version` | String | Required unless `version_file` is supplied | Nonempty project metadata, recorded in `clutch.lock`; not added as a compiler macro. Standalone projects do not require SemVer, but dependency version constraints do. |
| `version_file` | String path | Omitted; mutually exclusive with `version` | Existing text file containing canonical `major.minor.patch[-prerelease]`. Read relative to this manifest and fingerprinted as configuration input; also supplies a native dependency's declared version. |
| `language` | String | `"auto"` | `"c"`, `"cpp"`, `"c++"`, `"mixed"` or `"auto"`; `"c++"` is an alias for `"cpp"`. |
| `std` | Integer, string or table | C `11`; C++ `17` | Select language standards; see the rules below. |
| `entry` | String path | Omitted | Select one application source, optionally alongside `build.sources`. Suppresses implicit discovery. |

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
filter implicit discovery to that language, and must match explicitly selected
application source types. Tests may use either language independently.
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
| `generator` | String | Automatic selection | Native build-file format and executor family; the configuration CLI's `--generator` overrides it. Omit it to discover an available compatible generator/executor. |
| `sources` | Array of string paths or patterns | Discover recursively from the manifest directory when entry is also omitted | Select application files; a nonempty array replaces discovery. An empty array triggers discovery only without `project.entry`. |
| `include_dirs` | Array of string paths | Empty, plus an existing root `include/` | Existing directories added to every target/test in the project; workspace-root directories are shared with members. |
| `defines` | Array of strings | Empty | Preprocessor definitions for every target/test in the project; workspace-root definitions are shared with members. |
| `links` | Array of target names | Empty | Private library dependencies of the default application named by `project.name`; accepts qualified workspace names. |
| `pch` | Array of header paths, or `{ c = [...], cpp = [...] }` | Empty | Prefix headers for targets/tests; array form selects C++. |
| `runtime` | `"static"` or `"dynamic"` | `"static"` | Microsoft CRT: `/MT[d]` or `/MD[d]` for `cl`/`clang-cl`, or `-fms-runtime-lib` for GNU-style Clang targeting MSVC. Debug profiles select the debug runtime. No effect on MinGW or POSIX targets. |

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
Source arrays accept existing regular files and filename patterns.
Only the extensions above produce compiler actions. Headers and other matched
files appear as noncompiled project files in the file API and native IDE
projects; they do not create compilation-database entries. Clutch sorts and
deduplicates canonical paths, including overlaps between patterns and files.

```toml
[build]
sources = ["src/**.{cpp,c,h,hpp}", "support/*.cpp"]
```

`*.cpp` selects only files beside this manifest. `folder/*` selects only regular
files directly in `folder/`, and `folder/*.{cpp,c,h}` filters that directory's
files by suffix. `**.cpp` scans the manifest directory and all descendants;
`folder/**.{cpp,c,h}` and `folder/**/*.cpp` recurse from `folder/`.
`*` matches any filename text and `?` matches one filename byte. A double star
in the filename, or a `**` directory immediately before it, enables recursion.
Brace choices may be combined, but cannot be nested or empty. Directory
components must be literal, apart from an optional `**` directly before the
filename. Matching is case-sensitive on every platform. Use forward slashes
for portable manifest paths.

The same syntax works in `build.sources`, `target.sources` and `test.sources`,
including workspace members and native Clutch dependencies. `project.entry`
remains one explicit compilation unit. Each explicit pattern must match a
file; a compilable source is still required per target. Patterns do not add
include directories or infer which files belong to separate targets.

Wildcard scans skip `.git`, `.clutch`, the active profile's build directory,
directories with a Clutch ownership marker or `CMakeCache.txt`, and directory
links/junctions. File links are canonicalized and deduplicated. An expansion
allows at most 256 brace alternatives and scans at most 100,000 files per source
array. New or removed matches require another `clutch .`; changes to existing
headers/sources use the backend's normal dependency tracking.

Without `[[target]]`, all application sources form one executable and must provide
one entry point. When both sources and entry are omitted, discovery starts at
this manifest's directory and recurses. C selects `.c`, `.h` and `.H`; C++ selects
`.cc`, `.cpp`, `.cxx`, `.h`, `.H`, `.hh`, `.hpp` and `.hxx`; mixed/auto select both
sets. Headers remain noncompiled IDE files. Only recognized source/header
suffixes are discovered implicitly; explicit patterns may also select other files.
Nested directories containing their own `clutch.toml` are separate projects and
are not traversed implicitly, including workspace members. Generated-tree and
directory-link exclusions apply as above. Sources owned by declared tests and
other explicit targets are excluded from the implicit application.

At least one application source is required, even when tests are declared.
For several independent programs, declare targets/tests, split them into member
manifests, or select application sources explicitly. `entry = "main.cpp"` selects
just that file unless `build.sources` supplies additional files.

With explicit targets, `project.entry`, `build.sources` or nonempty `build.links`
enables the default application. It coexists with the declared libraries/executables. Files
found automatically are excluded from that application when another
explicit target owns them. Explicitly listed sources and the entry file remain
deliberate selections; Clutch does not remove them. Each target compiles each of
its sources only once.

`build.links` attaches libraries to the default application. If there is already
an explicit executable with the project's name, it receives these links and
the project source defaults instead of creating a second target. The library
and application must have different names. A virtual workspace root needs
`[project]` before it can declare application sources or links.

Header directories can be colocated with sources. An existing root `include/`
directory is automatically added, but that directory layout is optional. Headers
do not need to be listed as sources. Include paths become `-I<path>` or
`/I<path>`; definitions become `-D<value>` or `/D<value>`. Supply only the path or
definition, without these flag prefixes. Each definition is one argument; an
equals sign gives it a value, while a bare name defines the macro without an
explicit value in the command line.

Target/member include directories precede inherited workspace directories;
repeated paths are emitted once. Definitions are merged by macro name, with
later definitions replacing earlier ones: workspace defaults, then member
defaults, then target definitions. This avoids duplicate macro arguments when a
member overrides a shared value. Public requirements from linked libraries are
applied during target resolution; prefer consistent public macro values across
providers. Local member/target definitions override inherited public requirements
for that target's compilation; only declared public requirements are exported.

### Generators and compiler selection

Omitting `build.generator` and `--generator` enables automatic selection when
there is no generic build-tool override. Clutch tries a usable cached generator
first, then Ninja, GNU Make, NMake, JOM, Visual Studio and Xcode in that order,
skipping unsupported hosts, missing/old executors and incompatible compilers.
If none work, configuration reports the attempted choices without publishing a
build configuration. Explicit generator selections remain authoritative.

An explicit `--build-tool` or `CLUTCH_BUILD_TOOL` can identify a standard executor
family by its filename when the generator is omitted; custom-named wrappers
require a generator too. See [tool selection](usage.md#configure-then-build)
for override precedence and platform discovery.

| `generator` | Native output | Requirements |
| --- | --- | --- |
| `"ninja"` | `build.ninja` | Ninja 1.10+ and a compatible host compiler |
| `"make"` | GNU `Makefile` | GNU Make 4+, including compatible MinGW/MSYS variants; discovery prefers `gmake` |
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

## Configured files

Use `[[configure_file]]` to create a version header or other small text file
without a script interpreter. For example:

```toml
[project]
name = "hello"
version_file = "VERSION"

[[configure_file]]
input = "build_info.hpp.in"
output = "build_info.hpp"
git_version = true
variables = { API_LEVEL = "3" }
```

`VERSION` contains a line such as `1.2.3`. A template can contain:

```cpp
#pragma once
inline constexpr auto app_version = "@PROJECT_VERSION@";
inline constexpr auto api_level = @API_LEVEL@;
```

Clutch writes the output beneath the profile's `generated/` directory in an
automatically scoped subdirectory. Its owning project's targets can simply
`#include <build_info.hpp>`: the generated include root is added automatically.
Workspace members and native dependencies keep separate namespaces, even when
they choose the same output filename. Outputs are noncompiled IDE files and do
not add translation units to `compile_commands.json`.

| Field | Type / default | Meaning |
| --- | --- | --- |
| `input` | Required nonempty string path | Existing template, relative to its manifest or absolute. It is a configuration input. |
| `output` | Required nonempty relative string path | Name beneath this project's generated directory; absolute paths, escaping `..` paths and duplicate outputs are rejected. |
| `variables` | Table of strings; empty | Literal `@NAME@` substitutions. Keys are ASCII identifiers. Values are inserted once, without recursive expansion, shell execution or language-specific escaping. |
| `git_version` | Boolean; `false` | Resolve `PROJECT_VERSION` using local Git state. Requires a canonical release version. No fetching or network access. |

Built-in variables are `PROJECT_NAME`, `PROJECT_VERSION_BASE` and
`PROJECT_VERSION`. The latter normally equals the base version. With
`git_version = true`, a clean matching `v<version>` tag uses the release version;
other commits include `.dev`/`-dev` and a Git abbreviation, with `.dirty` and
`.shallow` where applicable. Without Git or in a source archive, it uses
`<version>+source`. Worktrees are supported; an unrelated parent repository is
not used. `PROJECT_` variable names are reserved.

Unknown well-formed `@NAME@` tokens are errors. `@@` writes a literal `@`;
Doxygen commands and other unpaired `@` text are retained. Template values are
plain text, so use values appropriate to the output language's quoting rules.

Configure again after changing a template or version file. Git commit/tag/dirty
state is refreshed during both CLI and direct native builds. Files are written
only when bytes change, preserving no-op builds; missing outputs are recreated.
Competing callbacks serialize publication and report a persistent writer lock
rather than waiting indefinitely.

Use configured files for simple text substitution, such as a version header or
feature constants. No interpreter is needed. Use [commands](#commands-and-lifecycle-hooks)
when a task needs a script, resource processor or other executable instead.
Neither facility is a declaration of compiler-generated source outputs.
Initial PCH inputs must already exist when the manifest is read.
Virtual workspace roots place templates in real members instead. Limits are
256 declarations per manifest, one MiB per input, four MiB per expanded output
and sixteen MiB per refresh batch. Source bundles retain the templates and
version files and regenerate their outputs after extraction.

## Commands and lifecycle hooks

Declare a named argument-vector command once, then run it manually or attach it
to one or more CLI lifecycle phases:

```toml
[[command]]
name = "generate-resources"
argv = ["python3", "scripts/generate.py", "@BUILD_DIR@/resources"]
working_dir = "@PROJECT_DIR@"
phases = ["before-build"]

[[command]]
name = "check-assets"
argv = ["node", "scripts/check-assets.mjs"]
# No phases: this command runs only through clutch exec.
```

```sh
clutch exec check-assets
clutch exec generate-resources -- --verbose
clutch build
```

| Field | Type/default | Meaning |
| --- | --- | --- |
| `name` | Required nonempty string | Unique name containing ASCII letters, digits, `_` or `-`; selects `clutch exec <name>`. |
| `argv` | Required nonempty string array | Executable followed by separate arguments. The executable must be nonempty; empty later arguments are preserved. Bare executable names use `PATH`; explicit relative executable paths resolve against `working_dir`. |
| `working_dir` | Nonempty string, `"."` | Child working directory, relative to the root manifest or absolute. It must exist when the command starts; Clutch does not create it. |
| `phases` | Array of strings, `[]` | Any of the four phases below. Duplicate phases and unknown names are errors. An empty list means manual-only. |

There are at most 256 commands per manifest and 1024 elements in each argv or
phase array. Unknown fields, duplicate names, wrong types and embedded NUL bytes
are rejected before hooks execute. `clutch exec` parses the root command table
without requiring source discovery, dependency resolution, compilers or saved
configuration. This makes it usable for generating initial source files before
the first `clutch .`. It accepts `--project`, `--release` and literal extra
arguments after `--`.

| Phase | When it runs |
| --- | --- |
| `before-configure` | After validating the manifest/sources and claiming the profile directory, before dependency resolution, configured-file rendering and compiler probes. |
| `after-configure` | After native rules, compilation database, graph, lock and tool selections are written, before committing saved configuration and a successful File API reply. |
| `before-build` | After target selection and optional artifact cleaning, before foreign dependency builds and native compilation/linking. |
| `after-build` | After a successful native build and runtime staging, before running an application, tests or a debugger. |

Build phases apply to `clutch build`, `run`, `test` and `debug`; they run once per
invocation, including a no-op build, rather than once per translation unit or
target. Hooks run serially in declaration order for each phase. A command with
multiple phases runs once in each matching phase. Hooks do not run for target
listing, formatting, linting, bundles, manual commands or internal callbacks.
Direct Ninja/Make/MSBuild/Xcode invocations do not execute these CLI hooks;
automatic Ninja/Make reconfiguration can invoke configure hooks through Clutch.
Whole-project CMake provider builds keep their own native custom commands.

Each `clutch watch` cycle invokes the ordinary configure/build/run/test workflow,
so matching hooks run for that cycle too. Use the watcher's `--path` option to
observe script/resource edits and `--ignore` for generated inputs that would
otherwise create a rebuild loop.

Every argument and working directory supports these single-pass substitutions:

| Token | Value |
| --- | --- |
| `@PROJECT_DIR@` | Absolute root manifest directory, including for workspaces |
| `@BUILD_DIR@` | Absolute selected profile directory; it exists for configure/build hooks |
| `@PROFILE@` | `debug` or `release` |
| `@@` | Literal `@`; use `@@PROFILE@@` to pass the token text itself |

Unknown paired tokens are errors. Substituted values and extra CLI arguments are
not recursively expanded. Other text is passed literally: `$HOME`, `%VAR%`, `*`,
`&&`, pipes and redirection are not interpreted. Commands inherit the environment
and standard streams. Python, Node and similar programs are needed only when a
declared command uses them; Clutch does not embed an interpreter. To use shell
syntax, explicitly invoke an installed shell and supply its command argument.

A failed hook stops the operation and preserves its child exit status. Later
hooks and application/test/debugger execution do not run. Configure failures
leave a profile-local pending marker; saved builds are blocked until a successful
`clutch .`, and active File API clients receive a failed configuration reply.
Commands' own filesystem side effects are not rolled back.

The root manifest owns commands for the entire workspace. Move member hooks into
the root rather than declaring them per member; dependency commands are not
inherited or automatically executed. Source bundles retain command declarations,
but opaque argv strings and external tool installations are not relocated or
packaged: prefer relative script paths/context tokens and retain needed scripts.

These hooks are lifecycle actions, not dependency-aware generated-output rules.
Configuration validates sources, include directories, PCH and template inputs
before running configure hooks. To create such initial inputs, first run a manual
`clutch exec` command. Do not rewrite manifests, version files or template inputs
from hooks: configuration fingerprints reject changes made during an operation.
Build hooks may refresh existing generated headers/resources, but cannot add
translation units to a saved graph. Reconfigure after changing source membership.
Commands should be repeatable; avoid overlapping operations in the same profile.
See the [runnable command example](../examples/commands/README.md).

## Precompiled headers

`build.pch` applies to targets and tests in the project. Workspace-root settings
are shared with members. `target.pch` and `test.pch` add target-specific headers.
Use an array for C++, or a language table for C and mixed projects:

```toml
[build]
pch = { c = ["common/c_prefix.h"], cpp = ["common/cpp_prefix.hpp"] }
```

Paths resolve relative to the declaring manifest and must name existing files.
For a Git import, `dependencies.alias.pch` resolves inside its selected checkout
and applies to consumers. For example:

```toml
[dependencies]
cli = { git = "https://github.com/CLIUtils/CLI11.git", tag = "v2.4.2", pch = ["include/CLI/CLI.hpp"] }
```

Clutch force-includes these headers before each source file of the selected
language. Select stable, compatible headers whose behavior does not depend on
macros defined inside individual source files. PCH is opt-in because arbitrary
third-party headers cannot safely be force-included. Repeated paths are deduplicated;
target-specific headers precede inherited build headers. Headers for an unused
language create no PCH action.

Each target/language/profile receives its own PCH using its exact compiler flags,
standards, includes and definitions. The selected generator schedules creation
before consumers, tracks included headers and rebuilds affected objects as needed.
GCC uses `.gch`, Clang uses `.pch`, and Microsoft-style drivers use `.pch` plus a
companion object. Visual Studio uses native create/use metadata; Xcode manages
prefix precompilation through its native build settings. No binary PCH is built
by configuration. Compilation databases use a textual copy of the same prefix,
so IDE parsing works before the first build.

## [dependencies]

Declare aliased Git, local-directory, installed-package or supported catalog
imports, with usage settings and optional consumer PCH headers. See [dependencies](dependencies.md) for every supported
field, transitive resolution, conflict/cycle diagnostics and offline operation.

## [profile.debug] and [profile.release]

Both profiles accept the same fields. Debug is selected by default;
`--release` selects release during configuration and execution. Profiles apply
their own defaults independently and do not inherit overrides from each other.
Custom profiles and per-test profile overrides are not implemented.

| Field | Debug default | Release default | Meaning |
| --- | --- | --- | --- |
| `build_dir` | `"Clutch-build-debug"` | `"Clutch-build-release"` | Nonempty relative/absolute output directory for native rules, caches, IDE metadata, objects and executable outputs |
| `optimize` | `"none"` | `"speed"` | `"none"`, `"speed"` or `"size"`; mapping below |
| `warnings` | `"extra"` | `"extra"` | Preset `"none"`, `"default"`, `"extra"`, or an array of warning names; mappings below |
| `debug_symbols` | `true` | `false` | Boolean controlling compiler/linker debug information independently of optimization and CRT selection |
| `safety` | Empty table | Empty table | Explicit static diagnostics and runtime detectors; described below |

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
The `debug_symbols` setting enables debugging information (`-g` or `/Z7`, plus the applicable
linker debug settings). Optimization and warning overrides do not change which
profile is active. Native IDE generators express corresponding settings through
their project formats while preserving Clutch's selected policies.

### Warning arrays

Use an array when you want individual categories instead of a preset:

```toml
[profile.debug]
warnings = ["extra", "shadow", "conversion", "no-unused", "error"]
```

An optional `"default"`, `"extra"` or `"all"` baseline is applied first. Choose at
most one baseline; omit it to keep the compiler's basic warning level. `[]` adds
no GCC/Clang warning switches and explicitly uses MSVC's `/W1` basic level. It is
different from `"none"`, which suppresses warnings. Baseline strings outside an
array retain their existing behavior.

Category entries then apply in array order. Prefix a category with `no-` to
disable it: `"no-shadow"`, for example. `"error"` promotes warnings to errors;
`"no-error"` restores ordinary warning severity. Exact duplicate names, multiple
baselines, unknown names, non-string entries and `"none"` inside arrays are errors.
Use the string preset `warnings = "none"` when suppression is intended.

| Array name | GCC / GNU-style Clang | Microsoft-style drivers |
| --- | --- | --- |
| `default` | `-Wall` | `/W3` |
| `extra` | `-Wall -Wextra` | `/W4` |
| `all` | `-Wall` | `/Wall` |
| `shadow` | `-Wshadow` | C4456, C4457, C4458, C4459 |
| `conversion` | `-Wconversion` | C4244, C4267, C4305 |
| `sign-conversion` | `-Wsign-conversion` | C4245, C4365 |
| `unused` | `-Wunused` | C4100, C4101, C4189, C4505 |
| `format` | `-Wformat=2` | C4475, C4477 |
| `deprecated` | `-Wdeprecated-declarations` | C4996 |
| `pedantic` | `-Wpedantic` | Configuration error: no supported equivalent |
| `error` | `-Werror` | `/WX` |

MSVC categories enable each listed diagnostic at level one (`/w1<number>`);
disabled categories use `/wd<number>`. GCC/Clang disabled categories use `-Wno-`,
with `no-format` mapped to `-Wno-format`. These are documented category mappings,
not a promise that different compilers diagnose exactly the same programs.
clang-cl keeps Microsoft-style baseline levels and uses Clang's diagnostic groups
for individual categories, passed through `/clang:`. Its category mappings follow
the GCC/Clang column, including `pedantic`; MSVC diagnostic numbers apply to `cl`.
In particular, GCC/Clang `-Wall` is a conventional group, not every warning; MSVC
`/Wall` includes diagnostics disabled by default. A compiler-native raw warning
flag is not accepted as a portable array name.

Workspace roots own the policy, and every native target/PCH receives the same
resolved warning options. Exact flags appear in the compilation database and IDE
project files. Foreign libraries retain their native warning policies. See the
[GCC warning reference](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html)
and [MSVC warning controls](https://learn.microsoft.com/en-us/cpp/build/reference/compiler-option-warning-level).

### Debug symbols

The debug profile defaults to symbols enabled and release defaults to disabled.
Set `debug_symbols = true` in release for optimized debugging, or set it to `false`
in debug to omit Clutch-generated symbols. This does not switch optimization,
`NDEBUG`, Microsoft CRT selection or the active build directory. Existing artifacts
such as a PDB from an earlier configuration may remain until cleaned; their mere
presence does not prove the current invocation generated symbols. Symbol format
and output paths follow the selected compiler/generator.

```toml
[profile.release]
optimize = "speed"
debug_symbols = true
```

| Compiler/generator | Current debug output |
| --- | --- |
| MSVC or clang-cl with an MSVC-style linker | `/Z7` embeds symbols in `.obj` files; `/DEBUG` produces a linker PDB for each executable or DLL. Static libraries retain their objects' symbols without a separate compiler PDB. |
| GNU-style Clang targeting the Microsoft ABI | `-g -gcodeview` emits CodeView information; the linker receives `/DEBUG` to produce a PDB for each executable or DLL. |
| GCC/MinGW or Unix Clang | `-g` requests debugging information, normally embedded DWARF in objects and the linked executable/library on these toolchains. A `.pdb` file is not expected from MinGW's usual debug configuration. |
| Xcode | Debugging symbols are enabled with `DEBUG_INFORMATION_FORMAT = dwarf`; Clutch does not currently request a separate `.dSYM` bundle. |

A PDB is a **Program Database** containing debug information. Clutch leaves its
name/location to the linker or native project defaults; it does not yet set an
explicit `/PDB:` path. PDBs and other optional symbol sidecars are not declared
outputs in Clutch's saved graph or file API. Deleting only a PDB therefore does
not reliably trigger relinking with every backend; use a clean debug build to
regenerate missing symbols. Keep an executable or DLL with the PDB from the same
build when debugging it elsewhere.

MSVC-style links use `/INCREMENTAL:NO` so each scheduled link consumes the current
objects and libraries. Ninja/Make/MSBuild still skip unchanged compilation and
link actions; the linker's separate `.ilk` cache is not used.

Other Clang target/linker combinations follow their toolchain's debug-format
defaults; selecting a compiler named `clang++` alone does not guarantee PDB output.

See Microsoft's documentation for
[`/Z7` and `/Zi`](https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format)
and [`/PDB`](https://learn.microsoft.com/en-us/cpp/build/reference/pdb-use-program-database),
and GCC's [debugging options](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html).

### Safety

Safety checks help find defects; they do not change C++ ownership rules or prove
memory safety. Static diagnostics run during compilation. Sanitizers instrument
the executable and report defects only when the relevant code runs. Exercise
instrumented applications with `clutch run` and `clutch test`.

```toml
[profile.debug]
debug_symbols = true

[profile.debug.safety]
sanitizers = ["address"]
strictness = "strict"
lifetime = false
```

| Field | Type/default | Behavior |
| --- | --- | --- |
| `sanitizers` | Array of strings, `[]` | `"address"` detects many out-of-bounds accesses and use-after-free; `"undefined"` checks supported undefined behavior; `"leak"` checks leaks at runtime; `"thread"` checks data races. Request only detectors supported by the installed compiler, target and runtime. |
| `lifetime` | Boolean, `false` | Enables Clang's newer C++ lifetime-analysis warning groups. Requires a compiler that actually accepts them; no version or filename assumption is made. C units are unaffected. |
| `strictness` | `"normal"` or `"strict"`, `"normal"` | Normal lifetime mode requests high-confidence `-Wlifetime-safety-permissive`. Strict mode requests `-Wlifetime-safety` and treats its diagnostics as errors. GNU-style sanitizer builds also disable recovery, and `clutch lint` treats selected analyzer warnings as errors. This is not a safety score or a guarantee that more defects will be found. |

#### Safety strictness levels

Only `"normal"` and `"strict"` are accepted. Choose the detectors separately:
strictness alone does not enable lifetime analysis or any sanitizer.

| Behavior | `"normal"` (default) | `"strict"` |
| --- | --- | --- |
| C++ lifetime diagnostics, when `lifetime = true` | Request the high-confidence `-Wlifetime-safety-permissive` group; diagnostics remain warnings unless another warning policy promotes them. | Request `-Wlifetime-safety` plus `-Werror=lifetime-safety`, making this group's diagnostics compile errors. |
| GNU-style sanitizer instrumentation, when requested | Use the detector's ordinary recovery/termination policy. Some detectors already terminate on an error. | Add `-fno-sanitize-recover=all` during compilation; detected nonrecoverable faults terminate when the program runs. |
| Microsoft-style sanitizer instrumentation | Use supported `/fsanitize=address` behavior. | Same runtime behavior; no extra recovery-control flag is added. |
| `clutch lint` | Keep the selected clang-tidy checks' ordinary severity and exit policy. | Add `--warnings-as-errors=*` to promote diagnostics from the selected checks. |

The lint rule applies even when `sanitizers = []` and `lifetime = false`.
Sanitizers report only while instrumented code executes, not before compilation.
Strict mode does not add a borrow checker, enable all clang-tidy checks, or assign
a safety score. Compiler/runtime support probes still apply in either mode.

Clutch compiles and links small probes during configuration to check requested
flags and runtime availability, without executing the probe. Unsupported requests
fail configuration instead of silently disappearing. Installed compiler/runtime
updates can change availability. Probes do not demonstrate that every defect is
detectable. Supported Windows Clang/MSVC AddressSanitizer DLLs are located in the
selected compiler installation and added to the normal runtime staging graph
beside executable targets. This does not change machine-wide PATH. Other
dependencies still need their normal runtime setup.

Some clang-cl installations reject AddressSanitizer with the debug CRT
(`/MTd` or `/MDd`). The probe reports that incompatibility without silently
changing your runtime selection. Use a supported compiler/profile combination;
for example, enable `profile.release.debug_symbols = true` and the release
profile's address sanitizer to debug an optimized instrumented build.

Microsoft-style drivers currently support the `address` detector through
`/fsanitize=address`. Other listed detectors require compatible GNU-style drivers
and runtimes. LeakSanitizer is not available on Windows. ThreadSanitizer cannot be
combined with address or leak detection; configure a separate build to use it.
Address/undefined detectors can be combined on supported toolchains. An explicit
lifetime request remains active even with `warnings = "none"`.

The workspace root owns safety policy. Native Clutch dependencies and PCH use the
same profile flags. CMake source adapters receive the requested safety flags;
upstream target overrides remain under CMake's control. Installed/prebuilt
libraries cannot be retroactively instrumented. Explicit Makefile adapters retain
their own build commands and flags; configure those libraries consistently when
whole-program instrumentation is needed. Full CMake project-provider workflows
continue to use their CMake presets/options instead of native Clutch profiles.

Clang's analysis uses annotations such as `[[clang::lifetimebound]]` supplied by
the library author. Clutch does not insert attributes or implement a custom borrow
checker. Lifetime analysis has limitations and can produce false positives;
experimental invalidation and translation-unit-wide inference are not enabled.
See the [Clang lifetime reference](https://clang.llvm.org/docs/LifetimeSafety.html),
[AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html),
[LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html) and
[Microsoft sanitizer documentation](https://learn.microsoft.com/en-us/cpp/sanitizers/asan).

Use distinct directories for debug/release and for different host/compiler
setups. There is no command-line build-directory override. Clutch marks owned
directories and refuses unrelated nonempty directories, redirected paths,
reserved source/metadata locations, or a build directory that contains an input
source file or include directory. `clutch clean` removes only the selected owned profile.
Reconfigure after cleaning or moving the project.

## Target

Use repeatable `[[target]]` tables for multiple executables or local libraries.
Without `build.sources`, `project.entry` or nonempty `build.links`, explicit
targets supply the project's source lists and do not create an additional executable. You can also keep the
application shorthand and add explicit libraries:

```toml
[project]
name = "calculator"
version = "0.1.0"
entry = "app/main.cpp"

[build]
links = ["math"]

[[target]]
name = "math"
type = "static"
sources = ["math/math.cpp"]
public_include_dirs = ["math"]
```

This creates `calculator` and `math`. An explicit executable named `calculator`
can add settings to the default application and may omit `sources` when project
source defaults supply them. All targets inherit project language standards and
the active profile. `build.include_dirs` and `build.defines` apply to all units.

Names must also avoid platform device names and generated metadata locations.

| Field | Type | Default | Behavior |
| --- | --- | --- | --- |
| `name` | String | Required | Letters, digits, `_`, `-`; unique across all targets and tests |
| `type` | String | `"executable"` | `"executable"`, `"static"` or `"shared"` |
| `sources` | Array of string paths or patterns | Required except for the executable matching `project.name` with project source defaults | Select target files using literal paths or local/recursive patterns; matching application defaults are merged and deduplicated |
| `include_dirs` | Array of string paths | Empty | Private header search directories |
| `public_include_dirs` | Array of string paths | Empty | Header directories used here and by consumers |
| `defines` | Array of strings | Empty | Private preprocessor definitions |
| `public_defines` | Array of strings | Empty | Definitions used here and by consumers |
| `links` | Array of target names | Empty | Libraries used privately |
| `public_links` | Array of target names | Empty | Libraries used here and exposed through this target's public interface |
| `default` | Boolean | `true` | Include ordinary target in an unqualified build |

```toml
[[target]]
name = "math"
type = "static"
sources = ["math/math.cpp"]
public_include_dirs = ["math"]
public_defines = ["MATH_API_VERSION=1"]

[[target]]
name = "calculator"
sources = ["app/main.cpp"]
links = ["math"]

[[test]]
name = "math_test"
sources = ["checks/math_test.cpp"]
links = ["math"]
```

`links` receives a provider's public requirements for this target's compilation.
Only `public_links` forwards those requirements to later consumers. Archives
contain their own objects; their private library dependencies still follow them
on the final link line. Shared libraries resolve private dependencies themselves.
Clutch orders libraries before their providers, including diamond dependencies,
and selects a C++ linker when a C executable uses C++ libraries. Unknown targets,
executable links, duplicate outputs and circular dependencies are rejected.

```sh
clutch targets
clutch build --target calculator
clutch run --target calculator
clutch test --target math_test
```

Repeat `--target` to build several targets. An unqualified build selects ordinary
targets with `default = true`; dependencies build as needed regardless of their
default setting. Tests remain opt-in. `run` requires exactly one selected/default
application executable; use `--target` when there are multiple applications.
Libraries do not need `main`, so library-only projects are supported.

Artifacts live in the profile build directory: executables use their target name,
static libraries use `lib<name>.a` (MSVC: `<name>.lib`), and shared libraries use
`lib<name>.so`, `lib<name>.dylib` or `<name>.dll`. Windows shared targets also
produce an import library. Export/import DLL symbols in your own headers using
`__declspec(dllexport)`/`__declspec(dllimport)` and private build definitions.
Clutch stages dependent DLLs beside test executables; POSIX targets use relative
runtime search paths. No system installation or global environment changes occur.
Distinct shared libraries consumed by the same executable or shared library must
have different filenames. Clutch rejects loader-name collisions during graph
validation, including names differing only by case on Windows. Qualified workspace
identities do not change the operating system's library filenames. Repeated local
names remain valid for static libraries and independent runtime closures.
Standalone `[[target]]` declarations for header-only interfaces or prebuilt
binaries, custom output naming, per-target standards and installation rules are
future work. Dependency adapters can already import native public interfaces
and prebuilt SDK libraries; see [dependencies](dependencies.md).

Both `[[target]]` and `[[test]]` accept `pch` with the syntax described
under [precompiled headers](#precompiled-headers).

## [[test]]

Repeat `[[test]]` once per independent executable test:

| Field | Type | Required/default | Behavior |
| --- | --- | --- | --- |
| `name` | String | Required | Nonempty letters/digits/`_`/`-`; must be unique among tests |
| `sources` | Nonempty array of string paths or patterns | Required | Select this test's C/C++ units and noncompiled files |

Tests also accept the target include/definition/link fields above. `type`, if
present, must be `"executable"`; `default` is not accepted. Names share the same
namespace as ordinary targets and the implicit application's name.

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
not linked automatically; list shared implementation sources or link a library target.
Test sources are deduplicated per target and may use either or both languages.

`clutch build` builds ordinary default targets. `clutch test` builds and runs all
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

General Bazel/Blaze adapters, arbitrary vcpkg recipes, folder aliases,
automatic dependency update notifications, custom build hooks, prebuilt binary
distributions and general cross-compilation configuration remain future work. Do not
add fields for these to an active manifest yet. Clutch's own configuration
engine and supported generators operate independently of the CMake project used
to develop Clutch itself.

Source ZIPs use the [`clutch bundle` command](usage.md#source-bundles); they do not
add manifest configuration fields or package prebuilt executables/installed SDKs.
