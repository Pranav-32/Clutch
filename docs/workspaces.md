# Workspaces

Audience: Users organizing projects across multiple manifests.

A workspace combines several projects into one configured build. Each member
has its own `clutch.toml` and targets. A workspace plays a similar composition
role to adding subdirectories in CMake, while using declarative project membership.
It does not execute CMake or interpret `CMakeLists.txt` files.

For a first example, configure and run the
[workspace sample](../examples/workspace/README.md). Contributors changing member
composition should use this page for the rules, then follow
[code layout](architecture.md#code-layout) and the `clutch.workspace` checks in
[testing](testing.md#select-checks).

## Root manifest

```toml
[workspace]
members = ["libraries/greeting", "applications/hello"]

[build]
generator = "ninja"

[profile.debug]
build_dir = "Clutch-build-debug"

[profile.release]
build_dir = "Clutch-build-release"
optimize = "speed"
```

`members` is a required, nonempty array of explicit relative directory paths.
Each directory must be inside the workspace and contain a regular `clutch.toml`.
Duplicate directories, redirected paths, duplicate project names, unknown keys
and nested workspaces are rejected. Globs, exclusions, outside members and
automatic upward workspace discovery are not implemented.

A root containing `[workspace]` without `[project]` is virtual: it coordinates
members and creates no implicit executable. Its `[build]` accepts `generator`,
`include_dirs`, `defines`, `pch` and `runtime`. Shared header paths resolve from
the root manifest.
A root may also declare a real `[project]` with its own sources/targets/tests.
That project's targets are qualified just like every other member's targets.

For either root layout, root `build.include_dirs` and `build.defines` provide
compile defaults for all workspace targets and tests. For example, add
`include_dirs = ["shared/headers"]` and `defines = ["WORKSPACE_API=1"]` under
the root `[build]` when that directory exists. An existing root `include/`
directory is shared too. Member and target include paths are searched first;
local macro definitions override shared values without duplicate compiler flags.
To keep a root project's setting private, put it on its explicit `[[target]]`
rather than the root `[build]`.

A real root can use `project.entry` and `build.links` alongside explicit targets
and workspace members. These describe its default application, named by
`project.name`; links to other members use qualified names as usual. An explicit
executable with that name can extend the application's settings instead.

## Member manifest

```toml
[project]
name = "greeting"
version = "0.1.0"
language = "cpp"
std = 17

[[target]]
name = "greeting"
type = "static"
sources = ["greeting.cpp"]
public_include_dirs = ["."]
```

Source/include paths resolve from the member's manifest directory. Its language
and standards apply to its own targets and tests. Existing implicit application
discovery works in members too, including application shorthand beside explicit
libraries. Member-wide include paths and definitions extend the root defaults
and stay private to that project's targets; public target requirements propagate
through declared links. Members retain their own language standards: root compile
defaults do not change a member's `project.std`.

The root owns profiles, build directories, optimization/warning/runtime policy and the
generator. Member `[profile]` tables and `build.generator` overrides are errors;
remove them when moving an existing project into a workspace. Compiler overrides
at configuration time apply to the entire workspace. Different compiler/ABI
families within one workspace are not supported.

## Target identity and links

Every workspace target has the identity `<project-name>::<target-name>`.
An unqualified `links` or `public_links` name refers to the current member.
Use a qualified name for another project:

```toml
[[target]]
name = "hello"
sources = ["main.cpp"]
links = ["greeting::greeting"]
```

Membership alone does not link projects. Providers must be library targets.
Missing/executable links and target dependency cycles across members are errors.
Target names may repeat in different projects; project names must be unique.

```sh
clutch .
clutch targets
clutch build --target hello::hello
clutch run --target hello::hello
clutch test --target hello::greeting_test
```

CLI target selection uses qualified names in a workspace. Default builds include
all ordinary default targets; tests remain opt-in. `run` requires one application
or an explicit selection. Programs and tests run from their owning member's root.

## Outputs, editors and changes

Each root profile contains one native backend configuration, compilation database,
DOT graph and [file-API](file-api.md) model covering the entire workspace. Artifacts
are separated under `members/<project>/`, tests under its `tests/`, and objects
under `obj/<project>/<target>/`. Public headers/definitions and member standards
agree between the database, file API and actual compile commands.

Configuration fingerprints every root/member manifest and `clutch.lock`. A build
rejects stale inputs even if their timestamps were preserved. Run `clutch .`
after member/target changes. Direct Ninja/GNU Make builds also watch member
manifests and can rerun configuration with recorded tool selections. Other
generators require explicit reconfiguration.

Commit the root [lock file](lock-files.md). `clutch clean` removes the selected
owned profile and preserves manifests, source files and the lock. It rejects a
profile containing a currently declared member.

All commands operate on the manifest directory explicitly selected by the caller.
Configuring a member directly treats it as a standalone project; cross-member
qualified links require configuration from the workspace root. Clutch does not
silently switch to a parent workspace.

See the [member workspace example](../examples/workspace/README.md) and
[flexible root example](../examples/flexible/README.md).

Git imports belong in real root/member manifests. Resolution combines their
constraints into one [package graph](dependencies.md) and source-root cache,
while compile settings remain scoped to the declaring project. A virtual root
may share `build.pch` with members just as it shares includes and definitions;
members and individual targets may add their own prefix headers.
