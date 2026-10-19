# Clutch file API v1

Audience: IDE integrators and contributors changing code-model output. This protocol
reference is not required for general contributions.

Clutch provides a versioned JSON query/reply interface for IDEs and developer
tools. Clients request build metadata; `clutch .` exports it from the same graph
used by native generators and `compile_commands.json`. Configuration does not
compile the project. Editor-specific settings remain the client's responsibility.

The protocol is inspired by [CMake's file API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html).
Clutch defines its own schemas. Its `codemodel` v1 is not CMake's `codemodel` v2,
and existing CMake clients need an adapter.

Target artifacts currently enumerate the primary binary/archive and a Windows
import library when applicable. Optional debug-symbol sidecars such as PDBs are
not yet enumerated. See [debug symbols](manifest.md#debug-symbols) before relying
on the API for debugger symbol-file discovery.

## Discover and request

`clutch capabilities` prints strict JSON with the Clutch version, file-API layout
version, supported query modes and supported object kinds/versions. It needs
neither a manifest nor a compiler. `clutch locate --project <source>` prints
strict JSON containing the canonical source root, active profile and absolute
build, query, reply and compilation-database paths. Add `--release` for release.
`locate` reads the manifest profile without configuring, finding tools or writing
files. Unsafe or unrelated build directories are rejected.

The location comes from the profile's `build_dir` in `clutch.toml`, including
custom paths and external build directories. Clients should use `locate` rather
than hard-coding a directory or creating their own TOML parser.

```sh
clutch capabilities
clutch locate --project .
```

Under the reported query directory, create a client-owned query:

```text
<build>/.clutch/api/v1/
  query/
    client-my-ide/query.json
```

```json
{
  "requests": [
    {
      "kind": "codemodel",
      "version": 1
    },
    {
      "kind": "toolchains",
      "version": [
        2,
        {
          "major": 1,
          "minor": 0
        }
      ]
    },
    {
      "kind": "inputs",
      "version": 1
    }
  ],
  "client": {
    "requestId": "my-ide-42"
  }
}
```

Then run `clutch <source>` using the intended compiler/generator and profile.
Require its successful exit before consuming a success reply. The native
executable needs no Python, editor, CMake or JSON runtime library to serve queries.

The [reference client](../examples/file-api/README.md) demonstrates this handshake
using Python's standard library. A [copyable query](../examples/file-api/query.json)
and [query schema](file-api/query-v1.schema.json) are included.

## Query rules

Each client uses a distinct `client-<name>` directory and may manage only its own
query files. Clutch reads queries without modifying them. Supported forms are:

| Location under `query/`                      | Form                                          |
|----------------------------------------------|-----------------------------------------------|
| `codemodel-v1`, `toolchains-v1`, `inputs-v1` | Empty shared stateless request                |
| `client-<name>/<kind>-v<major>`              | Empty client-owned stateless request          |
| `client-<name>/query.json`                   | Stateful JSON request with a `requests` array |

A stateless major version is an unsigned decimal integer with no leading zeros.
Stateful versions accept a nonnegative integer major, a `{major, minor}` object (minor defaults to zero), or an ordered
nonempty array of either form. A version
object specifies the minimum required minor. Clutch selects a supported major
and its current minor; currently every supported object is version 1.0. Floats,
strings, booleans, negative versions and nested version arrays are invalid.

Unknown fields are ignored for forward compatibility. Optional `client` values
at the query and request levels are echoed as JSON data without interpretation.
Requests are returned in their original order. No query can provide an output
path or cause a command to execute. Shared requests should be removed only with
coordination between their consumers.

Malformed JSON, duplicate keys, invalid UTF-8, invalid Unicode escapes, excessive
nesting and oversized files produce `invalid-query` errors for that query.
Requests receive `invalid-request`, `unknown-kind` or `unsupported-version`
diagnostics as appropriate. Unrecognized filenames receive `unknown-query`.
Diagnostics have the form `{"error":{"code":"...","message":"..."}}`;
codes are stable, while human-readable wording may improve. A malformed client
does not prevent valid independent requests from receiving replies.

Resource limits are one MiB per query, eight MiB per batch, 128 requests per JSON
query, 64 JSON nesting levels, 256 query files per batch and 256 entries per query
directory. Batch/directory limit violations fail configuration rather than
silently omit clients. JSON integer metadata uses 64-bit signed/unsigned values;
fractional metadata uses double precision. Clients needing lossless arbitrary
precision or exact textual identity should store those values as strings.

## Read an immutable snapshot

When queries exist, successful configuration creates files such as:

```text
reply/
  index-00000000000000000001.json
  generation-00000000000000000001/
    codemodel.json
    target-0.json
    target-1.json
    toolchains.json
    inputs.json
```

1. Refuse an active `configure.lock` in the API directory; wait or retry.
2. Select the lexicographically greatest `index-<20 decimal digits>.json`.
3. Check API major, `status`, source/build identity and profile.
4. Follow its `objects` and `reply` references. Read target objects through the
   code model's target references. Never construct object filenames yourself.
5. If a newer index or writer appears while reading, keep the old immutable
   snapshot as historical data or retry for the current configuration.

All reply filenames are relative to `reply/`, even target references located
inside a generation directory. Source, build, compiler and artifact paths are
absolute, with forward slashes and UTF-8 JSON strings. Indices and objects use
strict JSON without comments. A client should reject absolute references and
references escaping `reply/`. File paths are data and must not be shell-parsed.
Windows CLI/source-path handling still depends on the host's encoding setup;
the file API does not translate compiler environments or paths across hosts.

The index contains:

| Field                                    | Meaning                                                           |
|------------------------------------------|-------------------------------------------------------------------|
| `api`                                    | File-layout `{major, minor}` version                              |
| `clutch.version`                         | Producing Clutch executable's release/Git identity                |
| `generation`                             | Monotonically increasing, zero-padded decimal snapshot identifier |
| `status`                                 | `success` or `error`                                              |
| `profile`, `paths.source`, `paths.build` | Snapshot identity                                                 |
| `manifest`                               | Successful snapshot's FNV-1a manifest byte fingerprint            |
| `objects`                                | References to the unique requested top-level objects              |
| `reply`                                  | Query-directory structure, per-request responses and diagnostics  |
| `error`                                  | Configuration failure code/message when status is `error`         |

Each successful object reference has `kind`, `{major, minor}` `version`, and
`jsonFile`. Client query replies contain `requests`, ordered `responses` and any
query-level `client` data. Stateless responses are stored under their filenames.
Client-owned replies are nested under the client's directory name.

FNV-1a is a change detector, not a security checksum. The fingerprint's
`algorithm` is `fnv1a64`; `value` is 16 lowercase hexadecimal digits over the
manifest bytes. A reader should reject a changed manifest and request another
configuration. Source additions/removals and toolchain/environment changes also
require configuration; the file API is a snapshot interface, not a watcher.

## Object schemas

The [reply schema](file-api/reply-v1.schema.json) describes indices and objects.
All objects have `kind` and an independent object `version`.

### `codemodel` 1.0 and referenced `target` 1.0

`workspace` identifies workspace configurations. `projects` lists real projects
with `name`, declared `version`, and absolute `source` directory. A virtual root
is not a project entry. Workspace target names and IDs contain qualified
`<project>::<target>` identities, so repeated local target names remain distinct.

The code model exposes `project.name`, source/build `paths`, `profile`, generator
name/executor, absolute `compilationDatabase`, target references and project
configure/build `actions`. A target reference contains `id`, `name`, and
`jsonFile`; it implicitly references a `target` object with this model's major
version. Target IDs use `target:<unique target name>` and remain stable across
profile switches and source-list changes within a project.

Each target describes:

- `id`, `name`, `type` (`EXECUTABLE`, `STATIC_LIBRARY`, `SHARED_LIBRARY`) and `isTest`.
- `buildByDefault`: whether an ordinary target participates in an unqualified build.
- `project`, `localName`, `sourceDirectory`: owning project identity, local target
  name and member source root. Run actions use this member directory.
- `artifacts`: output paths, which need not exist before building.
- `dependencies`: direct linked target IDs; resolve them through codemodel references.
- `directory`: compiler/linker working directory.
- `sources`: source `path`, object path (`object`), `compileGroupIndex`, and the
  complete compiler `arguments` array, including its executable.
- `compileGroups`: `language` (`C`/`CXX`), `standard`, `includes` with `path` and
  `isSystem`, and raw `defines` without `-D` or `/D` prefixes.
- `link`: resolved linker/archiver arguments and working directory. Use the build
  action to build safely; archive replacement and runtime staging are backend steps.
- `actions.build`: backend invocation to build that specific target.
- `actions.run`: present only for executables, including test executables.

Actions contain `arguments` and `directory`. They are instructions for clients
to present or execute explicitly, not shell command strings. Build before run;
execute actions in the same compiler environment used during configuration.
Artifacts include Windows import libraries when applicable. Compile groups contain
the target's effective private/public and inherited public usage requirements.
This version 1 contract is being developed before its first release.

### `toolchains` 1.0

`toolchains` lists resolved language/compiler pairs with `language`,
`compiler.path` and `compiler.driverStyle` (`gnu` or `msvc`, including clang-cl).
Unused, unresolved language drivers are omitted. Vendor, compiler version, SDK
and cross-compilation target are not inferred from executable names.

### `inputs` 1.0

`configurationInputs` lists the manifest's absolute path and fingerprint.
It includes every root/member manifest and the root `clutch.lock`. Successful
indices also carry `configurationInputs` so a reader can reject stale member/lock
snapshots without separately requesting the inputs object. Fingerprints describe
the parsed configuration, not merely the last filesystem timestamp. Check all
entries when determining whether a reply is current.
`sourceDiscovery` describes the conservative `src/` directory watch: recursive
scan, supported extensions and `reconfigureOnAddRemove`. Clients may conservatively
watch it even when a manifest explicitly lists sources. These are configuration
inputs, not a compiler-discovered transitive header dependency database.

`sourceDiscoveries` adds per-project conservative `src/` watches with `project`,
`directory`, `recursive`, `extensions` and `reconfigureOnAddRemove`. The original
singular `sourceDiscovery` remains available for compatibility. Compilation groups
carry their owning member's standards and effective requirements. One workspace
database/model covers all members in the selected root profile.

## Publication, failures and ownership

Clutch permits an unowned build directory containing only the API query skeleton,
including one level of `client-*` directories. Unrelated files still prevent
Clutch from claiming the directory. Source/include containment and redirected
path checks continue to apply. `clean` requires the normal ownership marker;
an unconfigured query-only directory cannot be cleaned by Clutch.

The API is activated by a query directory. Once replies exist, subsequent
configuration runs publish a fresh index even if queries have been removed.
Projects that have never requested metadata create no `.clutch/api/` directory.
The query/reply mechanism remains client-driven during configuration. Existing
editor files are never created or modified by this interface.

A writer reserves `configure.lock` atomically and refuses concurrent writers
for that profile. Objects go into a new generation directory. A new index is
written to a staging file and renamed into `reply/` only after all referenced
objects are complete. Existing replies and client queries are never overwritten.
Incomplete generation directories are skipped by subsequent writers.

Failures after API initialization publish an `error` index with no object
references, superseding earlier success indices. Failures before a valid, owned
build directory/API session exists cannot publish an index. API I/O failures
may also prevent error publication. Always respect the configure exit status;
absence of a new index does not imply the last attempt succeeded.

The lock is released on normal return or exception. A terminated process can
leave it behind: verify that no configure process is using the profile before
manually removing the empty lock directory. Clutch does not guess whether a lock
is stale. Atomic visibility is provided on filesystems supporting same-directory
atomic rename; no power-loss durability or distributed/network locking is claimed.

Old indices and object generations are retained until `clutch clean` removes
the owned profile. This preserves objects for concurrent readers and avoids
guessing how long an IDE holds a snapshot. Clients own only their query files;
they must not prune reply files. Metadata storage grows with configurations while
the API is active, so normal profile cleanup also reclaims historical replies.

## Compatibility

Default applications created from project entry/source settings are ordinary
targets in the code model. Source arguments include inherited workspace compile
defaults and local overrides, using the same graph as the compilation database
and native builds. This does not introduce a separate target kind.

Layout major, object major/minor and Clutch release versions are independent.
Readers must tolerate unknown fields. Compatible additive changes increase the
object minor; incompatible semantics require a new major. Readers may request
supported major alternatives and a minimum minor. The public JSON interface
does not expose or stabilize Clutch's private `configuration.toml` cache.
