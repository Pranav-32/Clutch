# Lock files

Audience: Users reviewing and committing project resolution.

Configuration creates `clutch.lock` beside the selected root `clutch.toml` for
standalone projects and workspaces. Commit it with your source manifests.
It records resolution shared across debug/release profiles and developer machines.

For everyday use, start with [Configure and verify](#configure-and-verify).
Contributors changing resolution or serialization should also read
[what is locked](#what-is-locked-today) and the
[persistent-state contracts](architecture.md#persistent-state); the field tables
below describe the format rather than steps every contributor must follow.

Local-only resolution uses schema version 1. Projects with Git imports use
version 2, adding exact package pins. Both schemas contain `[[project]]` records:

| Field | Meaning |
| --- | --- |
| `name` | Unique project identity |
| `version` | Declared project version metadata |
| `path` | Root-relative project directory (`.` for a real root project) |
| `dependencies` | Sorted identities of directly linked other projects, including test links |

Records and links are sorted deterministically. A virtual workspace root has no
project record. Local project records use relative paths. The lock contains no compiler locations,
generator choice, profile flags, timestamps or binary artifacts. Git URLs are
recorded verbatim after local-path normalization; local repository URLs therefore
contain machine-specific absolute paths. Moving the checkout or
changing the host/profile does not change it when local resolution is unchanged.

## Configure and verify

```sh
clutch .                 # Create or update local resolution
clutch . --locked        # Require an existing matching lock
clutch . --release --locked
```

`--locked` is a configuration flag. A missing/outdated lock fails configuration
without rewriting it. Frozen checks compare parsed resolution data and preserve
existing comments, quoting and line endings. Normal configuration emits canonical
TOML with double-quoted, escaped strings, matching the manifest examples; it also
migrates older single-quoted locks. Canonical unchanged files keep their timestamps.
Update project versions, members and links in manifests,
then configure normally and review the resulting diff. Use
`clutch . --auto-update-dependencies` to refresh untagged imports within their
constraints; exact tags and revisions remain pinned.

Unchanged locks preserve their timestamps. Changed locks are published by atomic
replacement with an exclusive project-local writer directory. Interrupted writers
may leave `clutch.lock.write-lock/` beside `clutch.lock`; ensure configuration has
stopped before inspecting/removing that stale directory. Redirected lock paths, malformed TOML,
unknown schema versions and files larger than one MiB are rejected. A future
schema is never silently downgraded or overwritten.

The writer is shared across profiles and temporary. Git imports additionally use the source-root
`.clutch/deps/` cache; local-only configuration needs no source-root cache. File API clients create queries under
`<profile-build-dir>/.clutch/api/v1/query/`. When requested, configuration publishes
responses under `<profile-build-dir>/.clutch/api/v1/reply/`. Configuration without
queries does not create these API directories.

Build configuration also fingerprints the lock and all parsed manifests. Editing
or deleting them requires reconfiguration before `build`, `run` or `test`.
Cleaning a profile preserves the lock.

## What is locked today

Local projects remain live source directories. This lock records project identities,
declared versions and cross-project links; it does not freeze local source bytes,
pin compilers, or guarantee identical binaries. Source/header edits still build
incrementally without updating the lock. Target/source list changes require
reconfiguration, even when they leave this project-level resolution unchanged.
Root application `build.links` contributes cross-project edges just like explicit
target links. Shared include directories and macro overrides do not change this
project-level resolution, so an existing matching lock remains valid with
`--locked` after reconfiguring those compile settings.

Version 2 adds one `[[dependency]]` per selected Git repository:

| Field | Meaning |
| --- | --- |
| `id` | Stable source identity used for qualified imported targets |
| `git` | Repository URL or canonical local repository path |
| `rev` | Exact full Git commit ID |
| `version` | Declared native project version, semantic tag version, or `unversioned` |
| `ref` | Original selected ref or commit |
| `dependencies` | Sorted direct package IDs, including header-only imports |
| `requests` | Canonical incoming owner/alias/selector requirements |

Compatible pins are reused without querying the remote. `--locked` requires the
same request/resolution graph; `--offline` additionally prohibits downloads and
requires cached checkouts. A frozen configure can restore a missing cache from
exact commit pins when online. Compile-only import includes, definitions and PCH
settings do not reselect versions, but manifest edits still require configuration.
See [dependencies](dependencies.md) for version constraints and current limits.

Cleaning a build profile preserves the source cache. Dependency cache writer or
download directories left by an interrupted configure must be inspected after all
configuration processes have stopped. Do not edit published cached sources.

Version 3 adds live local-directory and installed-system records. Git-only graphs
retain version 2 and project-only graphs retain version 1. Version 3 records include
`provider` (`git`, `path`, or `system`). Non-Git records use `source` instead of
`git`, and an empty `rev`. Local `source` paths are relative to the root when they
share a filesystem volume; installed packages record their package identity and
reported version. Absolute SDK prefixes remain in machine-local configuration.
A changed installed version invalidates `--locked`; the lock does not freeze SDK
files or verify binary ABI compatibility. Cross-volume local paths are machine-local.

Named source-catalog imports normalize to Git pins; their incoming requests also
record the catalog name and exact baseline. The catalog itself is immutable cached
metadata, not an installed package tree. Changing its baseline requires a new lock.

## Difference from backend.toml

`<profile-build-dir>/backend.toml` is a machine-local cache of the selected generator
and executor path. `toolchain.toml` similarly caches compiler selection, and
`configuration.toml` stores resolved host commands. Keep these generated files in
ignored build directories. They are not portable dependency locks and should not
be committed. No manual edits to these cache files are required for workspace use.
