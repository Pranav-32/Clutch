# Lock files

Audience: Users reviewing and committing project resolution.

Configuration creates `clutch.lock` beside the selected root `clutch.toml` for
standalone projects and workspaces. Commit it with your source manifests.
It records resolution shared across debug/release profiles and developer machines.

The current schema is version 1. Repeated `[[project]]` records contain:

| Field | Meaning |
| --- | --- |
| `name` | Unique project identity |
| `version` | Declared project version metadata |
| `path` | Root-relative project directory (`.` for a real root project) |
| `dependencies` | Sorted identities of directly linked other projects, including test links |

Records and links are sorted deterministically. A virtual workspace root has no
project record. The lock contains no absolute paths, compiler locations, generator
choice, profile flags, timestamps or binary artifacts. Moving the checkout or
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
then configure normally and review the resulting diff. No separate update command
is implemented yet.

Unchanged locks preserve their timestamps. Changed locks are published by atomic
replacement with an exclusive project-local writer directory. Interrupted writers
may leave `clutch.lock.write-lock/` beside `clutch.lock`; ensure configuration has
stopped before inspecting/removing that stale directory. Redirected lock paths, malformed TOML,
unknown schema versions and files larger than one MiB are rejected. A future
schema is never silently downgraded or overwritten.

The writer is shared across profiles and temporary. It does not create a source-root
`.clutch` folder. File API clients create queries under
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

Git downloads and external dependency resolution are not implemented. The current
lock contains no Git revision pins.

## Difference from backend.toml

`<profile-build-dir>/backend.toml` is a machine-local cache of the selected generator
and executor path. `toolchain.toml` similarly caches compiler selection, and
`configuration.toml` stores resolved host commands. Keep these generated files in
ignored build directories. They are not portable dependency locks and should not
be committed. No manual edits to these cache files are required for workspace use.
