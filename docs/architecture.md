# Architecture

Audience: Contributors changing internals. Start with [Contributing](CONTRIBUTING.md)
for setup and a first PR.

Clutch configures C/C++ projects from `clutch.toml`, resolves a target graph,
and generates native build files and IDE metadata. CMake builds Clutch itself;
it is not Clutch's configuration engine. The root native manifest also supports
self-hosting after that bootstrap.

## Code layout

Headers live beside their implementations. Cross-module includes are relative
to `src/`. Each module documents its own functions and platform assumptions.

| Directory | Responsibility |
| --- | --- |
| `src/app/` | CLI parsing, project creation, workflow dispatch and optional formatter/analyzer/debugger adapters |
| `src/core/` | Manifest validation, build-directory ownership, graph construction and saved configuration |
| `src/core/targets/` | Target kinds, dependency resolution, archives and runtime library staging |
| `src/core/workspace/` | Member composition, input fingerprints and portable locks |
| `src/core/dependencies/` | Git identities, version constraints, backtracking and immutable checkout publication |
| `src/core/pch/` | Prefix-header parsing, isolated creator commands and textual IDE prefixes |
| `src/core/configured_files/` | Native template declarations, project-private outputs and optional Git version identity |
| `src/core/commands/` | Named argv commands, strict declaration parsing and CLI lifecycle execution |
| `src/app/watch/` | Bounded content snapshots, source/configuration change classification, debounce and serial child cycles |
| `src/core/file_api/` | Queries, versioned code models and snapshot publication |
| `src/core/safety/` | Lightweight profile safety records and manifest validation |
| `src/core/warnings/` | Portable warning-category validation and explicit native flag mappings |
| `src/toolchains/` | Compiler discovery, driver families, saved selections and safety capability probes |
| `src/backends/` | Generator registry, native build-file generation and execution |
| `src/providers/` | Whole-project delegation and evaluated metadata from existing build systems |
| `src/platform/` | Child processes, file output, JSON escaping and terminal messages |
| `third_party/` | Vendored headers, version notes and upstream licenses |
| `tests/` | Unit, integration and development-tooling checks |
| `.github/` | CI, version proposals and release packaging |
| `.devcontainer/` | Shared Linux development environment |

## Configuration and execution

Native manifests use Clutch's own configuration engine. The separate CMake
project provider configures an original top-level `CMakeLists.txt`, requests
CMake File API facts and routes operations to its owning CMake/CTest/CPack tools.
It does not feed a foreign application's source list into Clutch generators.
Source-local profile routing and build-local identity records prevent accidental
cross-project reuse; a pending marker blocks execution after failed configuration.
See [CMake projects](cmake-projects.md) for selection, presets and IDE boundaries.

`clutch .` validates manifests, resolves the selected profile and tools,
constructs the graph, and writes native rules. It also writes
`compile_commands.json`, `build-graph.dot` and saved configuration into the
profile's build directory. Configuration does not compile project sources.

Root-owned command hooks run after manifest validation and around configure/build
operations, once per CLI invocation. They use native child processes and inherit
the environment; no interpreter is embedded. Commands are persisted in schema 15
with the saved graph. Native dependency manifests do not contribute hooks.
Configure claims a pending marker before executing hooks and clears it only after
successful saved-state/File API publication; failed hooks cannot expose an older
graph as current. Post-build hooks complete before application/test/debugger
execution. Direct generator execution does not schedule these CLI hooks; they
are separate from dependency-aware custom-output rules.

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

`build`, `run` and `test` accept `--clean` as an execution policy. Ninja cleans
the selected producer closure; Make/NMake/JOM force its commands, and MSBuild
uses native Rebuild targets. GNU Make holds the already validated Makefile old
while forcing products, avoiding unintended regeneration of IDE snapshots.
Xcode uses controlled per-target intermediate/PCH directories and removes only
those directories and selected products after checking ownership and redirection.
The profile root is shared with metadata and must never be passed to recursive
product-directory cleanup. CMake/Make adapters retain explicit forced-build argv
alongside their ordinary actions in the private saved graph.
The generator registry owns optional cleanup hooks. `platform/artifacts.*`
preflights an explicit output list before removing anything; its caller establishes
profile ownership, and unlisted configuration and inputs remain intact.

## Persistent state

Configured files are rendered after output ownership is established and before
graph construction. Their inputs and optional version files are fingerprinted;
generated include roots and noncompiled IDE files are scoped by physical project
identity. The saved graph retains literal variables and output declarations.
Native executor callbacks refresh Git identity without rerunning configuration,
using one bounded writer and changed-content writes. Ninja uses restat outputs
and a separate order-only aggregation node; MSVC header nodes use the relative
spelling emitted by its dependency parser. Make dialects refresh before timestamp
evaluation, MSBuild before preparation and Xcode through an aggregate dependency.
No interpreter or nested CMake configuration participates in native self-hosting.

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

Dependency update reporting resolves a candidate graph in memory and compares its
Git records with existing pins. It may extend the immutable source cache to read
candidate manifests, but never publishes a lock, native configuration or API reply.
Transitive conflicts use the same backtracking solver as actual updates. Input
fingerprints and a before/after lock signature reject a changing comparison base.

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

Git checkouts use their stable revision paths from initialization onward. An
adjacent pending record proves ownership of an unfinished checkout; a verified
origin, commit and clean tree allow that record to become the completion marker
with one file rename. Source trees are never renamed for publication. This
supports Windows file readers and offline recovery after interrupted completion,
while existing complete cache entries keep their paths and metadata format.

The CMake adapter configures a small external consumer with the selected compiler
and profile. CMake evaluates imported targets and generator expressions; its file
API supplies public usage and transitive link inputs. Upstream sources keep their
native build graph. Makefiles have no standard interface metadata, so their
adapter requires explicit usage/artifact declarations and out-of-source rules.

For CMake targets carrying `INTERFACE_SOURCES`, an isolated static bridge compiles
the evaluated implementations with native rules. The probe link interface includes
that archive and transitive libraries. Compile groups are separated by language,
and compiler driver syntax is distinguished from the target ABI when selecting
the Microsoft runtime. Target discovery uses native directory properties before
selection; a sole library can be selected implicitly.
Clutch's own configuration engine remains responsible for the main project.

Installed packages use the same generic interfaces across vendors. Imports with
target/package discovery fields use CMake discovery; imports with only explicit
`include_dirs` and/or `libraries` use `dependencies/system.*` without a configuration subprocess.
`dependencies/interface.hpp` shares the evaluated interface between adapters.
Package names do not select special SDK behavior. Existing library files become
link prerequisites, while bare library names use the selected driver's spelling.

Evaluated interfaces propagate through native library targets. Saved configuration
retains foreign argv actions, artifact prerequisites and runtime paths. CLI builds
run reachable upstream actions before the main backend, using the same job budget
sequentially to avoid multiplying independent pools. Direct backend invocation
also prepares foreign actions through saved callbacks and stable public-interface
stamps. Ninja/GNU Make use explicit prerequisites, MSBuild shares preparation
targets, and Xcode uses aggregate dependencies. NMake/JOM perform preparation
while parsing their Makefile. Direct callbacks use one upstream job per native
scheduler slot and serialize profile-local upstream operations; CLI preparation
uses the requested job budget before handing off to the backend. IDE databases merge upstream records
with their original directories and exclude adapter probe sources.

## Packages, PCH and concurrency

Package resolution precedes native target construction. The package graph includes
header-only imports; the target graph contains concrete compiled libraries and
executables. Both reject cycles. The resolver groups incoming constraints by Git
source, prefers compatible lock pins, chooses the most constrained unresolved
package first, and backtracks with cached ref listings and parsed commit metadata.
Search and metadata limits bound untrusted or excessively broad input. Source
publication uses one project-local writer and immutable revision directories.
`dependencies/git_submodules.*` walks NUL-framed gitlinks with depth/count
limits, initializes missing worktrees at recorded commits and rejects modified
nested checkouts. Git resolves relative URLs; offline checks never clone/fetch.
Submodule files participate in source snapshots while Git metadata is omitted.

Native library manifests are composed as qualified targets with their own private
settings and standards. Public requirements propagate through the existing target
resolver. Header-only transitive interfaces are expanded once per consumer project.
The file API exports package records and PCH metadata; DOT output distinguishes
package edges, precompilation, compilation and linking.
Native action nodes have a separate DOT namespace from package records and expose
generated-header preparation edges even for interfaces without link artifacts.

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
