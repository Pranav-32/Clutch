# Clutch

A small native build system for C and C++ with a simple `clutch.toml` manifest.
Configure your project, then build, run and test it from the command line.
Clutch generates native build files and an IDE compilation database.

**Status: early development.** Clutch builds executable, static-library,
shared-library and test targets with local dependencies. Git dependencies, external-library build-system
adapters and self-hosting are planned; full CMake feature coverage is future work.

## What you can do

- Build C, C++ and mixed-language projects with explicit language standards.
- Declare multiple targets, link local libraries and propagate public headers
  and definitions. Select individual builds with `--target <name>`.
- Organize projects with `[workspace].members`, scoped member manifests and
  qualified cross-project target links.
- Generate a portable `clutch.lock` and verify local resolution with `--locked`.
- Use Ninja, GNU Make, NMake, JOM, Visual Studio/MSBuild or Xcode, depending on
  your operating system and installed tools.
- Keep debug and release builds separate, with simple optimization and warning
  settings in each profile.
- Generate `compile_commands.json` in the build directory for IDEs and language
  servers, alongside a Graphviz build graph.
- Serve versioned file-API queries for project, target and toolchain metadata.
- Create projects, build incrementally, run programs and filter executable tests.

## Install from source

From a checkout, you need CMake 3.20+ and a C++17 compiler. Clutch's parser
libraries are vendored, so this build requires no dependency downloads.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is usually `build/clutch` on Linux/macOS, `build/clutch.exe` with
Windows single-configuration generators, or `build/Release/clutch.exe` with
Visual Studio. Add its directory to `PATH` to use the commands below.

You also need a C/C++ compiler and the build tool selected for your projects;
Ninja is the default. See the [installation guide](docs/installation.md) for
platform setup, installation prefixes and Windows compiler environments.

## Quick start

With Clutch and Ninja available on `PATH`:

```sh
clutch new hello
cd hello
clutch .
clutch build
clutch run
```

`clutch .` reads `clutch.toml` and writes configuration into
`Clutch-build-debug/` without compiling. `clutch build` compiles and links the
application. To use the release profile:

```sh
clutch . --release
clutch build --release
clutch run --release
```

Configure again after changing the manifest or adding/removing source files.
Existing source and header edits only need another build.

## A simple manifest

A C++ project with `src/main.cpp` can use:

```toml
[project]
name = "hello"
version = "0.1.0"
language = "cpp"
std = 17

[build]
generator = "ninja"
sources = ["src/main.cpp"]

[profile.debug]
build_dir = "Clutch-build-debug"

[profile.release]
build_dir = "Clutch-build-release"
optimize = "speed"
warnings = "extra"
```

Add files to `sources` for a multi-file executable, or omit it to discover
sources under `src/`. Select compilers during configuration, for example
`clutch . --cc clang --cxx clang++`. Run `clutch generators` to see available
generators and `clutch --help` for commands.

For local libraries and multiple executables, use explicit `[[target]]` entries.
See the [library example](examples/targets/README.md) and
[target reference](docs/manifest.md#target). The optional file-API sample uses
Python; Clutch itself serves IDE metadata directly in native C++.

## Workspaces and lock files

Split a larger repository into projects with their own `clutch.toml` files:

```toml
# Root clutch.toml
[workspace]
members = ["libraries/greeting", "applications/hello"]

[build]
generator = "ninja"

[profile.debug]
build_dir = "Clutch-build-debug"

[profile.release]
build_dir = "Clutch-build-release"
```

Each member defines its project and targets. Use `links = ["greeting::greeting"]`
to consume a library from another member. Source paths and standards belong to
each member; profiles and generator selection belong to the root. Configuration
produces one build graph, compilation database and file-API model for the workspace.

```sh
clutch .
clutch targets
clutch run --target hello::hello
clutch test --target hello::greeting_test
clutch . --locked
```

Configuration generates `clutch.lock`; commit it with the manifests. `--locked`
requires an existing matching lock. It currently records local project versions,
relative paths and cross-project links. Local source files remain editable; Git
revision pinning will arrive with external dependency resolution. Build-directory
files such as `backend.toml` remain machine-local caches and stay out of Git.

IDE query/reply files live under `<build-directory>/.clutch/api/v1/` when
requested; configuration does not create an unused API directory. Generated lock strings use
double quotes; configuring normally also updates older lock formatting.

Try the [workspace example](examples/workspace/README.md), or read the
[workspace guide](docs/workspaces.md) and [lock-file guide](docs/lock-files.md).

## Learn more

| Guide | What it covers |
| --- | --- |
| [Installation](docs/installation.md) | Build and install Clutch on your platform |
| [Runnable examples](examples/README.md) | Try single-file, multi-file, C and mixed-language projects |
| [Using Clutch](docs/usage.md) | Configuration, compilers, generators, IDE setup and project tests |
| [Manifest reference](docs/manifest.md) | Every supported field, default and example |
| [Workspaces](docs/workspaces.md) | Member manifests, qualified links, shared profiles and build outputs |
| [Lock files](docs/lock-files.md) | Portable local resolution, `--locked` and dependency-pinning limits |
| [File API](docs/file-api.md) | Query/reply protocol for IDE and developer-tool integrations |
| [Versions and releases](docs/releases.md) | Git version identity, automated checks and release downloads |

## Contribute

See [Contributing](docs/CONTRIBUTING.md) for development setup, code conventions,
formatting and review checks. The [development-container guide](docs/development-container.md)
provides a shared Docker environment for Linux, macOS and Windows hosts.
Implementation details live in the [architecture guide](docs/architecture.md).

Report vulnerabilities privately through the [security policy](.github/SECURITY.md).

## License

Clutch is licensed under [BSD 3-Clause](LICENSE).
Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors.
See [licensing](docs/licensing.md) for redistribution requirements and
third-party notices.
