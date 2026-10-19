# Dependencies

Audience: Contributors working on dependency resolution and adapters; users
can use the examples and field reference when adding a library.

A dependency is code your project uses: a header directory, a compiled library,
or an SDK already installed on the machine. Add it under `[dependencies]` in the
`clutch.toml` that uses it. Clutch resolves these declarations during
configuration, then passes their headers, definitions and libraries to your
project's compiler.

For a compiled library, **linking** combines its compiled code with your
program. A library's **public interface** is the set of headers, definitions and
other libraries its consumers need. An **SDK** (software development kit) can
provide these files as an existing installation instead of source code.

## Contributing to dependency support

For your first contribution, follow [Contributing](CONTRIBUTING.md), then use the
[architecture guide](architecture.md) to understand the full configuration flow.
Configuration reads declarations, resolves sources/versions, evaluates external
interfaces and constructs the target graph used by generators and IDE metadata.
Within `src/core/dependencies/`, start with the module matching your change:

| Module | Responsibility |
| --- | --- |
| `manifest.*` | Read dependency fields, validate combinations and resolve declaration-relative paths |
| `sources.*`, `git.*`, `vcpkg.*` | Locate live sources, fetch exact Git revisions and interpret catalog entries |
| `version.*`, `resolve.*` | Compare versions, resolve transitive requirements and reject conflicts/cycles |
| `cmake.*`, `make.*`, `system.*` | Obtain external library interfaces from native metadata or explicit declarations |
| `foreign.*`, `interface.hpp` | Apply evaluated interfaces and schedule reachable foreign build actions |

Use [the dependency test suites](testing.md#select-checks) for changes here;
the [package-example runner](testing.md#package-examples) also checks the actual
published examples. Update this reference whenever a field or path rule changes.

## Start with a working example

Build/install Clutch and put it on `PATH`, then run these commands from this
repository's root:

```sh
clutch examples/dependencies
clutch run --project examples/dependencies
clutch test --project examples/dependencies
```

The application prints `Local packages: 42; installed SDK: 1.0.0`; the test checks
the same libraries independently. This example needs a C++17 compiler, Ninja
and CMake 3.21+, but no downloads or SDK installation. Its manifest combines a
plain header directory, a native Clutch library and a small example installation
prefix.

Read the [example walkthrough](../examples/dependencies/README.md) to try each
source separately, including GitHub, a local Git repository, GNU Make, a named
source catalog and machine-level SDKs. Python is used only by the optional
helper that creates demonstration Git repositories; Clutch itself is native.

## Choose where the library comes from

Each dependency must select **exactly one** source field:

| Field | Use it when… | What Clutch does |
| --- | --- | --- |
| `path` | You already have the library's source directory | Reads the files in place; edits remain live |
| `git` | The library lives in a remote or local Git repository | Resolves a revision and keeps a verified checkout in the project's `.clutch/` cache |
| `system` | Headers or compiled libraries are already installed | Discovers package targets or uses your explicit interface; installs nothing |
| `vcpkg` | A supported entry in a pinned vcpkg source catalog identifies the upstream repository | Reads source coordinates and imports that Git repository; does not run vcpkg |

The name on the left is your **alias**. In `cli = { ... }`, the alias is `cli`.
Choose letters, digits, underscores or hyphens. It names the dependency in
diagnostics and library links; it does not change the library's header names.
For example, importing CLI11 as `cli` still uses `#include <CLI/CLI.hpp>`.

Git 2.29+ is needed for Git/catalog imports. Local and installed dependencies
do not need Git. A library's source location and its build system are separate:
a Git or local-directory import can use Clutch, CMake, Make or just headers.

## Add a local directory

For a library with headers only:

```toml
[dependencies.headers]
path = "third_party/headers"
build_system = "header-only"
include_dirs = ["include"]
```

Here `path` is relative to the manifest containing this declaration.
`include_dirs` is relative to that library directory, so the compiler searches
`third_party/headers/include/`. A header-only library has no compiled archive
to link and needs no `clutch.toml`.

For a library with its own Clutch manifest:

```toml
[dependencies.math]
path = "../math"
build_system = "clutch"
target = "math"
```

`target` selects the library exported by the other project's `[[target]]`.
Its public headers, definitions and library links propagate to your targets.
It is linked automatically; you do not need to list the same alias in `links`.
See [native libraries](#native-clutch-libraries) for multiple exported targets.

## Add a Git or GitHub library

This declaration fetches a fixed CLI11 release and exposes its headers:

```toml
[dependencies.cli]
git = "https://github.com/CLIUtils/CLI11.git"
tag = "v2.4.2"
build_system = "header-only"
include_dirs = ["include"]
```

GitHub uses the same `git` field as any other Git server. You can use an HTTPS
URL, SSH URL or an existing local repository path. Relative repository paths
resolve from the declaring manifest. Use Git credential helpers or SSH for
private repositories; embedded HTTP credentials are rejected.

After editing your manifest, configure before building:

```sh
clutch .
clutch build --jobs 4
```

Configuration resolves dependencies and writes native rules plus
`compile_commands.json` in the selected profile's build directory. It also
records exact revisions in the source-root `clutch.lock`. Commit the lock with
your manifest and ignore `.clutch/` and generated build directories.

An ordinary configure retains compatible locked revisions. It does not silently
follow every new commit. See [version selection](#git-version-selection) and
[updates](#locks-cache-and-offline-use) when you want to change a pin.

## Use a library's own build system

### Automatic selection

With `build_system = "auto"` (the default), Clutch looks for `clutch.toml`,
then `CMakeLists.txt`, then `Makefile`. With none of these, it treats the source
directory as header-only. Set `build_system` explicitly when a repository has
several build descriptions or automatic selection is unsuitable.

Clutch configures the main project itself. An adapter lets an external library
keep its own build rules; it does not make CMake or Make the main project's
configuration engine.

### CMake libraries

A CMake **target** is a named library with associated headers, flags and link
requirements. Select the target documented by the upstream library:

```toml
[dependencies.fmt]
git = "https://github.com/fmtlib/fmt.git"
tag = "11.0.2"
build_system = "cmake"
target = "fmt::fmt"
cmake_options = { FMT_TEST = "OFF", FMT_DOC = "OFF" }
```

Use `targets = ["Vendor::Core", "Vendor::Extras"]` instead of `target` when you
need several CMake libraries from the same import. Do not set both fields.

CMake 3.21+ configures the library under `<build>/.clutch/foreign/`. Clutch
supplies the selected compilers, profile and Microsoft runtime policy. Ninja
is preferred for this foreign build when available, independently of your main
project's generator.

Clutch reads CMake's evaluated target interfaces, rather than guessing public
settings from compilation commands. Public includes, definitions, compile
options, language requirements and transitive libraries reach your code.
Private upstream settings stay with the library. An upstream public language
requirement can raise the consumer's C/C++ standard. When CMake exports a
compilation database, its real source records are merged into Clutch's build-local
database with their original working directories and private flags.

Use `clutch build`, `clutch run` or `clutch test` to build these projects.
Clutch first builds reachable foreign libraries, then invokes its native
backend. Running the generated Ninja/Make project directly currently skips this
foreign preparation. Configuration does not build the library's final artifacts,
although CMake may compile compiler-detection probes and execute upstream
configure logic. Use dependencies whose build scripts you trust.

A local library works the same way: replace `git` and `tag` with
`path = "third_party/fmt"`. See the
[self-contained CMake example](../examples/foreign/clutch.toml).

### Makefile libraries

Makefiles have no standard interface describing exported headers or libraries.
Supply that information explicitly:

```toml
[dependencies.legacy]
path = "third_party/legacy"
build_system = "make"
target = "library"
include_dirs = ["include"]
libraries = ["libexample.a"]
```

Here `target` is the Make goal, `include_dirs` points into the source tree,
and `libraries` names files the Makefile will produce in its isolated build
directory. These fields do not mean “scan all compiler commands for a library.”
Use `targets` for several Make goals, or omit both goal fields to let Make use
its default goal.

GNU Make runs out of source with `-f <source>/Makefile`, `SOURCE_DIR`,
`BUILD_DIR`, `VPATH`, `CC`, `CXX` and `-j <jobs>`. The upstream Makefile
must support this layout and quote/escape paths containing spaces.
`include_dirs = ["{build}/generated"]` can expose generated headers.
`build_arguments` passes additional individual arguments; `{source}` and
`{build}` expand to absolute paths. It is not a shell command or hook.

Clutch does not infer interfaces with `make -n`. An in-source-only Makefile
needs an upstream wrapper or out-of-source rules. Try the
[Make example](../examples/dependencies/makefile/clutch.toml) on a POSIX host.

### Native Clutch libraries

A native import needs `[project]` and static/shared `[[target]]` libraries.
Applications and test targets are not imported. Choose `target = "name"` when
several libraries are exported. An explicit `links = ["alias::other_library"]`
can address another exported native library; `links = ["alias"]` refers to the
selected one.

Each library keeps its own standard and private compile settings; the consuming
profile selects optimization and warnings. Public interfaces propagate through
library links. Unreachable dependency libraries are not built. Imported Clutch
manifests currently support one project, without a nested `[workspace]`.

Native dependency sources accept the same
[recursive source patterns](manifest.md#sources-and-headers) as application
targets, for example `sources = ["src/*.{cpp,c,h}"]`. External adapters leave
source selection to the library's own build system.

## Installed SDKs and packages

`system` means “use an existing installation.” It is not restricted to a list
of package or SDK names, and it never installs or modifies an SDK. Choose one
of the following two forms according to the metadata the installation provides.

### Discover exported package targets

Use this form when the package documents a CMake `find_package` name and
imported targets:

```toml
[dependencies.sdk]
system = "VendorSDK"
root = "/opt/vendor-sdk"
target = "Vendor::Core"
components = ["Core"]
version = "2.1"
```

These are illustrative vendor names; replace them with the package's documented
values. `system` supplies the default package name. `root` is an optional
existing installation prefix and can be relative to the declaring manifest.
Omit it to use CMake's normal search locations. `target` selects an imported
library; `components` asks for required package components. Omit optional fields
your package does not support.

`cmake_package` overrides the discovery name if you want a different logical
`system` identity. `version` is passed to CMake using its package-version/range
syntax; the package's own discovery implementation determines compatibility.
This form requires CMake 3.21+. The
[installed-prefix example](../examples/dependencies/clutch.toml) ships a tiny
package config so you can try it without installing an SDK.

### Describe headers and libraries explicitly

Use this form when the installation has no usable package target metadata:

```toml
[dependencies.sdk]
system = "vendor-sdk"
root = "../installed/vendor-sdk"
include_dirs = ["headers"]
libraries = ["lib/vendor.lib"]
defines = ["VENDOR_STATIC=1"]
```

Replace these example paths and the library filename with your installation's
values.
`system` is an arbitrary identity here. Relative paths use `root` when supplied,
otherwise the declaring manifest's directory. `libraries` can name existing
library files or bare linker names. A bare name such as `"vendor"` becomes
`-lvendor` for GCC/Clang or `vendor.lib` for Microsoft drivers. The compiler's
normal library search environment must locate bare names; `root` alone does not
add a linker search directory. Prefer an explicit file for a private installation.

For headers only, omit `libraries`. For libraries already on the compiler's
search path, `include_dirs` can be omitted. At least one must be present.
This form needs neither CMake nor Git, and cannot use discovery fields
`version`, `components`, `cmake_package` or `cmake_options`. Do not combine
explicit `libraries` with `target`/`targets`.

See the [explicit header example](../examples/dependencies/system-interface/clutch.toml)
and [machine-level SDK examples](../examples/dependencies/README.md#machine-level-sdks).
The Windows SDK example links `d3d12` with this same generic mechanism; its name
does not trigger special behavior.

### Match the host installation

Choose a compatible compiler, architecture and runtime for prebuilt libraries.
For Microsoft drivers, `build.runtime = "static"` (default) selects
`/MT[d]`; `"dynamic"` selects `/MD[d]`. Match the library's build.

On Windows, imported shared-library metadata can provide runtime DLL paths.
Use `runtime` for DLLs the package does not expose. Clutch stages declared DLLs
beside reachable executables and rejects conflicting destination filenames.
On other platforms, the host loader still needs to find external shared libraries.

The [CUDA example](../examples/dependencies/cuda/clutch.toml) consumes a host
library from an installed CUDA Toolkit. Compiling `.cu` files, selecting GPU
architectures and device linking are not implemented. A lock file does not freeze
an SDK's installed files or guarantee ABI compatibility on another machine.

## Dependency field reference

Fields below belong inside `[dependencies.alias]` or an equivalent inline table.
String values use quotes; string arrays look like `["first", "second"]`.
Fields are optional unless marked required. Unknown fields are errors.
Omit optional string fields you do not need instead of assigning `""`; array
entries must also be nonempty. CMake option values may be empty strings.

### Source and revision fields

| Field / type | Meaning, default and valid use |
| --- | --- |
| `path` / string | Source directory read in place, relative to the declaring manifest or absolute. Must exist. Select exactly one of `path`, `git`, `system`, `vcpkg`. |
| `git` / string | Remote Git URL or existing local repository path. Relative paths use the declaring manifest. Clutch downloads only the selected revisions into its local cache. |
| `system` / string | Installed-package identity. With `target`/`targets`, also the default CMake discovery name. Without targets, labels the explicitly declared headers/libraries. |
| `vcpkg` / string | Port name in the limited pinned source catalog. Requires `baseline`; does not require the vcpkg executable. |
| `tag` / string | Exact Git tag, such as `"v1.2.3"`. Only for `git`; mutually exclusive with `branch` and `rev`. |
| `branch` / string | Git branch. First resolution pins its current commit; normal configure keeps that pin. Only for `git`. |
| `rev` / string | Full 40- or 64-hex-digit Git commit ID. Abbreviated hashes are rejected. Only for `git`. |
| `version` / string | Git/native local SemVer constraint; installed discovered package version/range; or check on the catalog's selected version. No constraint by default. Not supported for explicit system interfaces. See the version rules below. |

### Build and interface fields

| Field / type | Meaning, default and valid use |
| --- | --- |
| `build_system` / string | How source libraries are interpreted: `"auto"` (default), `"clutch"`, `"header-only"`, `"cmake"` or `"make"`. Installed packages accept `auto`/`cmake`; presence of imported targets selects discovery. |
| `target` / string | Selected native library, CMake library target or Make goal. A native import defaults to its only exported library; otherwise select one. CMake imports require `target` or `targets`. Make uses its default goal when neither is set. |
| `targets` / string array | Several CMake library targets or Make goals. Alternative to `target`, not an additional list. Native Clutch imports select one library with `target`; use explicit alias-qualified links for others. |
| `include_dirs` / string array | Public header search folders. Source-relative for Git/path imports; SDK-prefix/manifest-relative for system imports. Header-only imports default to `include/` if present, otherwise the source root; an explicit list overrides that default. For Make, `{build}` can refer to generated headers. |
| `defines` / string array | Extra definitions passed to consumers, without `-D` or `/D`; for example `["VENDOR_STATIC", "VENDOR_LEVEL=2"]`. Default: none. |
| `pch` / string array or table | Headers force-included and precompiled for consumers. An array selects C++; `{ c = ["prefix.h"], cpp = ["prefix.hpp"] }` separates languages. Paths use the source or SDK prefix. Default: no PCH. See [precompiled headers](manifest.md#precompiled-headers). |
| `libraries` / string array | For Make: output file paths relative to its isolated build directory. For explicit system imports: existing library paths or bare linker names. Not a general extra-link-flags field. |
| `runtime` / string array | Extra runtime DLL files to stage on Windows. Make paths are build-relative; other paths use the source directory or installed SDK prefix. Default: only runtime files supplied by imported target metadata. |
| `build_arguments` / string array | Extra Make arguments, each a separate argument. `{source}`/`{build}` expand to absolute directories. Default: none; Make only. |

### Discovery and catalog fields

| Field / type | Meaning, default and valid use |
| --- | --- |
| `root` / string | Existing SDK installation prefix for `system`, or local Git catalog repository for `vcpkg`. Relative to the declaring manifest. Not valid for ordinary Git/path imports. |
| `cmake_package` / string | CMake package discovery name for an installed dependency. Defaults to `system`. Requires imported target discovery. |
| `components` / string array | Required components requested from an installed CMake package. Names come from that package's documentation. Default: none. |
| `cmake_options` / table of strings | Upstream CMake cache options, for example `{ FMT_TEST = "OFF" }`. Values must be strings, including `"ON"`/`"OFF"`. Clutch reserves `CMAKE_*`, `VCPKG_*` and `BUILD_TESTING` for its own compiler/generator policy. |
| `baseline` / string | Required full 40-hex-digit catalog repository commit for a `vcpkg` import. Changing it is an explicit manifest edit. |
| `features` / string array | Recognized for diagnostics, but catalog feature selection is not implemented. Omit it. |
| `triplet` / string | Recognized for diagnostics, but catalog triplet selection is not implemented. Omit it. |

Absolute paths are accepted where a path is expected. For relative paths, the
declaring manifest is the starting point for `path`, local `git` and `root`;
the selected library/prefix is the starting point for its interface paths.
A Make adapter's output paths are the exception: they refer to its build directory.
Do not copy host-specific absolute paths into a shared manifest unless intentional.
Use forward slashes in TOML paths on Windows too, or TOML literal strings when
you need unescaped backslashes; double-quoted strings interpret backslash escapes.

## Optional named source catalog

A catalog helps select an upstream repository by a package name. This declaration
shows the shape; replace all placeholder values:

```toml
[dependencies.utility]
vcpkg = "your-port-name"
baseline = "<full-40-digit-catalog-commit>"
target = "Utility::Utility"
```

By default, Clutch reads the pinned commit of Microsoft's vcpkg Git repository.
An optional `root` selects a local catalog repository instead. The adapter reads
literal `vcpkg_from_github`/`vcpkg_from_git` source coordinates, then imports the
upstream Git revision. Direct `git` imports bypass this catalog entirely.

This is a limited source catalog, not support for arbitrary vcpkg recipes.
Clutch does not execute port scripts, install vcpkg, select triplets or use an
installation tree. Patched ports, default features, port-specific transitive
dependencies, computed fetch declarations and installation-specific behavior
are rejected. Host dependencies on vcpkg CMake helper ports are allowed, without
running those helpers; the upstream adapter owns the build.

The pinned baseline selects one port version; Clutch does not search vcpkg's
version-history database. Declare upstream options through `cmake_options`.
Use a direct Git import if a port needs unsupported recipe behavior. Try the
[local catalog example](../examples/dependencies/README.md#local-git-repositories-and-named-catalogs)
before choosing an external port.

## Git version selection

Choose at most one of `tag`, `branch` or `rev`. You can also add a `version`
constraint to check that the selected revision is compatible.

| Declaration | First resolution | Later normal configuration |
| --- | --- | --- |
| `tag = "v1.2.3"` | That exact tag | Retains the locked commit |
| `rev = "<full-commit>"` | That exact commit | Retains that commit |
| `branch = "main"` | Current commit on the branch | Retains a compatible pin |
| `version = "^1.2.3"` without an exact selector | Highest compatible stable SemVer tag | Retains a compatible pin |
| No selector or version | Remote default branch's current commit | Retains the pin |

SemVer constraints require full `major.minor.patch` versions:

| Constraint | Accepted versions |
| --- | --- |
| `"1.2.3"` or `"=1.2.3"` | Exactly 1.2.3 |
| `"^1.2.3"` | At least 1.2.3, below 2.0.0 |
| `"^0.2.3"` | At least 0.2.3, below 0.3.0 |
| `"^0.0.3"` | Exactly 0.0.3 |
| `"~1.2.3"` | At least 1.2.3, below 1.3.0 |
| `">=1.2.0 <2.0.0"` | Both comparisons must hold; a comma separator also works |
| `"*"` | Any stable semantic version |

Tags may start with `v`. Build metadata does not affect ordering. Prereleases
need an explicit prerelease comparator with the same major/minor/patch.
Partial versions, wildcard components and `||` alternatives are rejected.
Installed CMake packages use their discovery version rules instead of this
SemVer grammar. A local native package checks its declared project version;
a header directory without version metadata cannot provide that guarantee.

## Transitive resolution

A **transitive dependency** is a dependency of another dependency. Clutch reads
the selected source's root `clutch.toml` recursively. A header-only repository
can declare just `[dependencies]`; select `build_system = "header-only"`
explicitly for such a manifest.

One version is selected per repository identity across aliases and workspace
members. Compatible requests share a checkout. If one version introduces a
conflict, the resolver tries other compatible versions. An unsatisfied graph
reports the requesters and their constraints; a dependency cycle is an error,
including cycles involving only headers.

Use the same Git URL spelling throughout a graph. Different transport URLs are
not assumed to identify the same repository; local paths are canonicalized.
Search is bounded at 1,024 packages and 10,000 decisions. If those limits are
reached, narrow version constraints or simplify the graph.

## Locks, cache and offline use

A **lock** records the resolution to reuse; a **cache** stores the downloaded
source files. They serve different purposes:

| Item | Location | Put it in Git? |
| --- | --- | --- |
| Resolved dependency lock | Source-root `clutch.lock` | Yes, for shared project dependencies |
| Downloaded Git sources | Source-root `.clutch/deps/` | No |
| Native rules and IDE database | Profile's build directory | No |

```sh
clutch .                           # Resolve missing imports; retain compatible pins
clutch . --locked                  # Require unchanged resolution
clutch . --locked --offline        # Also require already-cached Git sources
clutch . --auto-update-dependencies # Deliberately refresh eligible imports
```

`--auto-update-dependencies` refreshes branches and imports without an exact
tag/revision, within their constraints. Exact tags and commits remain pinned;
edit their selector to change them. The resolver checks the whole graph before
writing a new lock. Review the lock diff. Do not combine this option with
`--locked` or `--offline`.

A locked configuration can fetch a missing exact revision when the server still
serves it; offline configuration cannot. Catalog imports need both the catalog
and upstream revisions cached. Offline mode blocks Clutch's Git transfers and
requests disconnected CMake FetchContent behavior. Custom upstream download
commands need their own offline settings: this is not a network sandbox.

Cached checkouts must retain their recorded origin, commit and clean Git state.
Do not edit or generate outputs in them. Use a live `path` import when developing
a library. A project-local writer lock protects publication; if an interrupted
operation leaves a lock directory, inspect it and confirm no writer is active
before removing it.

Local source paths remain live, and installed libraries remain machine-local;
the lock does not freeze their contents. Local Git repository origins are stored
as absolute, machine-specific paths. Prefer remote URLs for a shared project's
Git dependencies. See the [lock format](lock-files.md) for recorded fields.

## Troubleshooting and current limits

| Symptom | What to check |
| --- | --- |
| Compiler cannot find a header | Use the library's real include spelling. Check `include_dirs` against the source/prefix directory, then configure again. |
| CMake cannot find a package or target | Check the upstream package name, target name, installation prefix and selected compiler environment. |
| Linker cannot find a bare system library | Initialize the SDK/compiler environment or use an existing library file path. `root` alone does not add library search paths. |
| Make produces no declared library | Check its goal, out-of-source support and build-relative `libraries` paths. |
| Dependency conflict or cycle | Follow the diagnostic's requester chain; align constraints or remove the circular import. |
| Lock/cache check fails | Review manifest/lock changes and cached checkout state. Use normal configuration to fetch missing sources, or restore the required offline cache. |

Automatic update notifications, Git submodules, general Bazel/Blaze adapters,
arbitrary vcpkg recipes and multiple simultaneous versions of one repository
are not implemented. Unsupported adapters fail explicitly.
