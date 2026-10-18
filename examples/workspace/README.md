# Workspace with a local library

Run these from this directory using a built Clutch executable on `PATH`:

```sh
clutch .
clutch targets
clutch run --target hello::hello
clutch test --target hello::greeting_test
clutch . --locked
```

The root lists two member directories. Each member owns its sources and standard;
the root selects the generator and profile build directories. `hello` explicitly
links `greeting::greeting`, receiving its public header directory. Membership
alone does not establish a dependency.

Configuration writes one compilation database and build graph in the root's
profile directory. Artifacts are separated under `members/<project>/`. It also
generates a portable `clutch.lock` at the workspace root; commit that file with
the manifests. `--locked` verifies it without changing resolution.

See [workspaces](../../docs/workspaces.md) and [lock files](../../docs/lock-files.md)
for semantics, migration, limits and independent project use.
