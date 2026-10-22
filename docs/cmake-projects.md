# Working with an existing CMake project

Audience: users adopting Clutch around a CMake project, and contributors changing
the project provider. For importing a library into a native Clutch project, see
[dependencies](dependencies.md) instead.

Clutch can drive an existing `CMakeLists.txt` without converting it to TOML.
CMake still evaluates the project and owns its build rules. Framework helpers,
generated resources, subdirectories, compiler settings and application bundles
continue to work through the project's original configuration.

## Configure, build and run

Install CMake 3.21 or newer and the compiler, SDKs and tools your project needs.
From a directory containing `CMakeLists.txt`:

```sh
clutch .
clutch targets
clutch build --jobs 4
clutch run --target my_app -- --application-option
clutch test
```

Clutch selects the CMake provider automatically when `CMakeLists.txt` exists and
`clutch.toml` does not. When both exist, the native Clutch manifest takes
precedence. Use `--provider cmake` on configuration and execution commands to
select CMake explicitly; `--provider clutch` selects the native manifest.

Without a preset, the default build directories are `Clutch-build-debug` and
`Clutch-build-release`. Configure and execute the latter with `--release`.
`--generator` accepts CMake's exact generator name for this provider:

```sh
clutch . --generator Ninja --cmake-arg=-DCMAKE_CXX_COMPILER=clang++
clutch . --release --generator "Visual Studio 18 2026"
clutch build --release
```

Use a generator supported by your installed CMake and host. This is independent
of the [native Clutch generators](usage.md#generators). CMake performs compiler
and Visual Studio installation discovery. A project that uses external scripts
or tools still requires those tools; Clutch adds no Python requirement.

`--cmake-arg` is repeatable. It accepts inline `-D`, `-U`, `-W`, `-A`, `-T` and
`--toolchain=` settings. For example, use
`--cmake-arg=-DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake` or
`--cmake-arg=-Ax64`. Quote the complete option when its value contains spaces.
Use a fresh build tree when changing incompatible compiler/generator settings,
as required by CMake. Source and build routing flags are not forwarded here.

For a multi-configuration generator, use `--configuration <exact-name>` to
configure or select a reported configuration, such as `RelWithDebInfo`.
The default is `Debug` if available. Single-configuration presets retain their
reported configuration. `--release` selects the separate release profile's
routing record; it does not rewrite a preset's cache settings.

`run` requires an unambiguous executable or one `--target`. The executable path
comes from CMake's File API, including its evaluated output name. Build failures
stop execution, and an automatic CMake regeneration refreshes the artifact
metadata before launch. Application arguments follow `--`.

`clutch build --clean` and `clutch run --clean` use CMake's `--clean-first`.
`clutch clean` invokes the native clean target. These operations leave the
configuration and source dependencies in place.

## Existing presets

Use your project's configure preset directly:

```sh
clutch . --preset development
clutch build
clutch run --target my_app
```

CMake resolves the preset's binary directory, inheritance, toolchain, conditions
and environment. Clutch adds its File API queries through a generated preload
script; it does not wrap the project in another `project()` or replace the
preset's generator, compiler, standard or runtime. Compilation-database export
is requested for the non-preset default configuration; a preset can enable
`CMAKE_EXPORT_COMPILE_COMMANDS` itself.

Configure, build, test and package presets have independent names. Explicit
execution presets are delegated as independent native workflows:

```sh
clutch build --preset development-build
clutch test --preset development-tests
clutch package --preset development-package
```

Those commands use the preset's own directory, configuration, environment,
filters and concurrency. They do not infer a name from the configure preset,
or combine preset behavior with `--clean`, `--configuration`, `--target`,
`--release`, `--filter` or `--jobs`. Define such settings in the execution preset. Unlike the
saved-tree workflow, a test/package preset does not implicitly build first.
`run` uses the saved configuration rather than an execution preset. Package
presets require a CMake/CPack version supporting that preset format.

## Tests, installation and packaging

For the saved build tree, `clutch test` builds first, then invokes CTest with
failure output enabled. A missing or empty test selection fails clearly.
`--filter` matches a literal substring of the CTest name. CTest retains ownership
of test commands, environment, fixtures, timeouts and working directories.

```sh
clutch test --filter audio --jobs 4
clutch install --prefix /path/to/stage --component Runtime
clutch package
```

`install` builds first and delegates to `cmake --install`; omit `--component` to
install all components. `package` builds first and invokes the generated
`CPackConfig.cmake`. The project must define its packaging, normally through
`include(CPack)`; Clutch does not invent an installer or change signing rules.
These commands are currently CMake-provider operations.

## IDE metadata and failure handling

`clutch locate` reports the provider, source/build directory, configuration,
current CMake File API index and compilation-database path (or `null`). IDEs can
read the original `<build>/.cmake/api/v1/reply` data, including target compile
groups and configure inputs. The native Clutch File API remains a separate
protocol for native projects. CMake only exports exact `compile_commands.json`
files with generators that support that feature, such as Ninja and Makefiles;
Clutch does not fabricate one for Visual Studio or Xcode.

Source-local `.clutch/cmake/` records route each profile to its configured build
tree. The build's `.clutch/provider.json` records its identity. Keep `.clutch/`
untracked. A failed or interrupted configuration blocks saved-tree execution
until configuration succeeds again. Concurrent Clutch workflows fail clearly
rather than sharing a partially updated build tree. Native tool failures retain
their exit codes. After changing source directories or deleting a build tree,
configure again to refresh routing.

Complete-project delegation preserves CMake behavior; it does not translate
arbitrary CMake functions into native Clutch declarations. Calls such as
`juce_add_gui_app(...)` belong in the existing CMake project. For gradual
migration, native Clutch targets can separately consume evaluated CMake library
interfaces through the dependency adapter. An arbitrary executable target is
not treated as a library or reconstructed from its compilation database.
