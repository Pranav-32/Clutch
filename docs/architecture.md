# Architecture

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

`build`, `run` and `test` load the saved graph and invoke the selected backend.
They reject changed manifests or locks using content fingerprints. Source/header
edits are incremental; changing the manifest or discovered source set requires
configuration. Direct Ninja/GNU Make invocations can rerun configuration when
manifest timestamps change, using recorded tool selections.

## Persistent state

| Location | Contract |
| --- | --- |
| Source-root `clutch.lock` | Portable local project resolution; commit with manifests |
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
