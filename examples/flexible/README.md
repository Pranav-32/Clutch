# Flexible manifests

This example combines a root application, an explicit static library, a workspace
member and a linked test. The root keeps `project.entry`; `build.links` connects
that application to both libraries without a separate application target table.

From this directory, with Clutch and Ninja on `PATH`:

```sh
clutch .
clutch run --target flexible::flexible
clutch test
clutch . --release --locked
clutch run --release --target flexible::flexible
```

The program prints `Hello from a flexible workspace: 3`. The shared header
directory and `EXAMPLE_SCOPE=1` definition come from the root `[build]`. The member
overrides the macro to `2`, and the root library overrides it to `3`. Compile-time
checks verify that each scope receives its intended value. These private overrides
do not propagate through library links; public headers use `public_include_dirs`.

All workspace target names are qualified, including the root application.
Profiles and the generator belong to the root; the member keeps its own language
standard. See the [manifest reference](../../docs/manifest.md) and
[workspace guide](../../docs/workspaces.md) for the full rules.
