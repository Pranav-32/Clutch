# Architecture

Audience: Contributors changing internals. Start with [Contributing](CONTRIBUTING.md)
for setup and a first PR.

Clutch configures C/C++ projects from `clutch.toml`, resolves a target graph,
and generates native build files and IDE metadata. CMake builds Clutch itself;
it is not Clutch's configuration engine.

## Code layout

Headers live beside their implementations. Cross-module includes are relative
to `src/`. Each module documents its own functions and platform assumptions.

| Directory | Responsibility |
| --- | --- |
| `src/app/` | CLI parsing, project creation and workflow dispatch |
| `src/core/` | Manifest validation, build-directory ownership, graph construction and saved configuration |
| `src/core/targets/` | Target kinds, dependency resolution, archives and runtime library staging |
| `src/core/workspace/` | Member composition, input fingerprints and portable locks |
| `src/core/dependencies/` | Git identities, version constraints, backtracking and immutable checkout publication |
| `src/core/pch/` | Prefix-header parsing, isolated creator commands and textual IDE prefixes |
| `src/core/file_api/` | Queries, versioned code models and snapshot publication |
| `src/toolchains/` | Compiler discovery, driver families and saved selections |
| `src/backends/` | Generator registry, native build-file generation and execution |
| `src/platform/` | Child processes, file output, JSON escaping and terminal messages |
| `third_party/` | Vendored headers, version notes and upstream licenses |
| `tests/` | Unit, integration and development-tooling checks |
| `.github/` | CI, version proposals and release packaging |
| `.devcontainer/` | Shared Linux development environment |

## Configuration and execution

`clutch .` validates manifests, resolves the selected profile and tools,
constructs the graph, and writes native rules. It also writes
`compile_commands.json`, `build-graph.dot` and saved configuration into the
profile's build directory. Configuration does not compile project sources.

The graph holds compiler argument vectors, target artifacts, usage requirements
and dependency edges. Public requirements propagate through declared library
links. Workspace targets use qualified identities and retain their owning
project's paths and standards. Duplicate output producers and dependency cycles
are rejected before generation and after loading saved state.

Application shorthand is normalized alongside explicit targets before resolving
links. Root workspace include directories and definitions provide defaults for
all members. `core/targets/requirements.*` applies local include precedence and
merges definitions by macro identifier, so native rules, saved commands and IDE
metadata share one resolved policy. Public library interfaces stay separate from
private project defaults.

`build`, `run` and `test` load the saved graph and invoke the selected backend.
They reject changed manifests or locks using content fingerprints. Source/header
edits are incremental; changing the manifest or discovered source set requires
configuration. Direct Ninja/GNU Make invocations can rerun configuration when
manifest timestamps change, using recorded tool selections.

## Persistent state

| Location | Contract |
| --- | --- |
| Source-root `clutch.lock` | Local project edges and exact Git pins; commit with manifests |
| Source-root `.clutch/deps/` | Verified project-local Git source cache; ignore in version control |
| Build-directory `configuration.toml` | Private resolved graph and input fingerprints |
| Build-directory `toolchain.toml` / `backend.toml` | Host-specific compiler and executor selections |
| Build-directory `.clutch-build-owner` | Ownership marker used by configuration and cleanup |
| Build-directory `.clutch/api/v1/` | Public query/reply interface, created when requested |

Private cache schemas may change and require reconfiguration. The public
[file API](file-api.md) and [lock format](lock-files.md) have independent versions.
Changed file contents are written only when necessary to preserve incremental
behavior. Build directories must not overwrite inputs, adopt unrelated contents
or redirect through links; cleanup verifies ownership.

Native tools track source/header dependencies. Make command changes also
invalidate affected artifacts and their consumers so coarse timestamps cannot
preserve an obsolete archive or executable. Windows shared-library consumers
use import libraries, and dependent runtime DLLs are staged beside test programs.
Symbol sidecars have [separate limitations](manifest.md#debug-symbols).

Generated metadata and graph output paths reject symlinks and redirected parent
directories before writes or build execution. Writers also check stream close
failures. These checks protect owned outputs from accidental redirection; they
are not a filesystem sandbox against another process changing paths concurrently.

Paths crossing CLI, environment, argument, TOML or JSON boundaries use UTF-8;
filesystem operations keep native `std::filesystem::path` values. Windows CLI
arguments are normalized from the wide command line. `platform/environment.*`
copies native Unicode environment values, including compiler and executor paths.
Use `fs::u8path` to decode text and `u8string`/`generic_u8string` to serialize it.

`platform/arguments.*` separates command-line quoting from compiler response-file
encoding. Microsoft response files carry a UTF-8 byte-order mark; GNU-style
response files use their own escaping rules. NMake rules also carry the marker
so prerequisite filenames are decoded correctly. Generators share these encoders
instead of maintaining competing implementations.

ELF shared libraries carry a loader filename so relative runtime paths remain
effective after moving an artifact tree. Runtime closures reject distinct shared
libraries with the same loader filename; independent consumers can still use
repeated local names. Saved host configuration remains bound to its build path.

## Changing the implementation

Resolve manifest semantics before generator output. Carry new values through
the graph, saved state, compilation database and file API consistently. Update
the private cache schema when persisted semantics change. Keep the public
protocol and its JSON schemas compatible or version changes explicitly.

A backend owns its file format, discovery, host restrictions and invocation;
the CLI must not assume Ninja arguments. Test generator behavior on its native
host, including unchanged rebuilds and changed headers/flags. See
[testing](testing.md) and [contributor conventions](CONTRIBUTING.md).

## Native dependency adapters

Source selection and build adapters are separate. `dependencies/sources.*` routes
live directories and installed packages without discovering Git, while Git owns
immutable source publication. `dependencies/vcpkg.*` reads a pinned source-catalog
entry without executing its port script or invoking vcpkg. Unsupported recipe
requirements fail rather than silently changing their meaning.

The CMake adapter configures a small external consumer with the selected compiler
and profile. CMake evaluates imported targets and generator expressions; its file
API supplies public usage and transitive link inputs. Upstream sources keep their
native build graph. Makefiles have no standard interface metadata, so their
adapter requires explicit usage/artifact declarations and out-of-source rules.
Clutch's own configuration engine remains responsible for the main project.

Installed packages use the same generic interfaces across vendors. Imports with
`target`/`targets` use CMake discovery; imports with explicit `include_dirs` and/or
`libraries` use `dependencies/system.*` without a configuration subprocess.
`dependencies/interface.hpp` shares the evaluated interface between adapters.
Package names do not select special SDK behavior. Existing library files become
link prerequisites, while bare library names use the selected driver's spelling.

Evaluated interfaces propagate through native library targets. Saved configuration
retains foreign argv actions, artifact prerequisites and runtime paths. CLI builds
run reachable upstream actions before the main backend, using the same job budget
sequentially to avoid multiplying independent pools. Direct backend invocation
currently does not run that preparation. IDE databases merge upstream records
with their original directories and exclude adapter probe sources.

## Packages, PCH and concurrency

Package resolution precedes native target construction. The package graph includes
header-only imports; the target graph contains concrete compiled libraries and
executables. Both reject cycles. The resolver groups incoming constraints by Git
source, prefers compatible lock pins, chooses the most constrained unresolved
package first, and backtracks with cached ref listings and parsed commit metadata.
Search and metadata limits bound untrusted or excessively broad input. Source
publication uses one project-local writer and immutable revision directories.

Native library manifests are composed as qualified targets with their own private
settings and standards. Public requirements propagate through the existing target
resolver. Header-only transitive interfaces are expanded once per consumer project.
The file API exports package records and PCH metadata; DOT output distinguishes
package edges, precompilation, compilation and linking.

PCH creator actions copy target flags and are isolated by target and language.
Compiler PCH capabilities are checked after external interfaces have supplied
their headers, so installed and foreign libraries follow the same policy.
IDE commands retain textual prefix headers, independent of binary artifact state.
Ninja/Make rules add explicit creator prerequisites; Visual Studio and Xcode use
their native prefix-header facilities. Native executors own scheduling and header
tracking. Build execution supplies a CPU-derived job limit unless the user selects
one explicitly; NMake retains its serial semantics.

`core/source_files.*` expands source selections during configuration. One shared
reader serves applications, targets, tests, workspace members and native imports.
Canonical paths are sorted and deduplicated. Compile units feed the build graph;
noncompiled selections feed native IDE projects and the file API's `additionalFiles`.
Directory listings are reused across brace alternatives and repeated patterns
within one expansion, preserving the scan bound without repeated filesystem walks.
Changing pattern membership requires reconfiguration.

The GNU Make backend selects `gmake` before `make` and requires version 4 or
newer, including when executing saved configurations. Prefix-list bytes also
have a private content signature: changing them invalidates the PCH, its compile
units and consuming products before Make parses rules, independent of timestamp
precision. Unchanged prefixes retain their artifacts.

## Source snapshots

`core/bundle/` resolves cached dependency pins, maps physical source roots into
archive paths, and rewrites participating Clutch manifests without changing the
original tree. Local imports already inside the project retain their location;
external sources receive private snapshot paths. The recipient regenerates build
rules for their compiler and location.

`platform/zip.*` streams stored ZIP records with CRC-32 checks, fixed timestamps
and bounded classic-ZIP fields. Publication uses a sibling staging directory and
a no-replace hard link, so a failed/racing writer cannot expose a partial archive
or replace an existing one. The destination filesystem must support hard links.
See [source bundles](usage.md#source-bundles) for exclusions, relocation limits
and external tool/SDK prerequisites. The native ZIP tests check publication/path
contracts; Python integration tests independently parse CRCs and rebuild extracted
Git/local/CMake snapshots after their original directories have been moved.
