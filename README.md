# Clutch

<p align="center">
  <img src="assets/branding/clutch-banner.png" alt="Clutch — C / C++ Build System" width="960">
</p>

Clutch is a small native build system for C and C++ with a simple
`clutch.toml` manifest. Configure a project, then build, run and test it from
the command line. Clutch generates native build files and an IDE compilation
database, with support for libraries and multi-project workspaces.

Clutch configures your project itself. You choose the compiler and a native
generator such as Ninja, GNU Make, Visual Studio or Xcode, subject to host support.
It can also [drive an existing CMake project](docs/cmake-projects.md), keeping
that project's native rules, framework helpers and presets intact.

**Status: early development.** This README describes the checkout. See
[feature status](#feature-status) for the tagged baseline and newer capabilities.

## Install from source

You need CMake 3.20+ and a C++17 compiler. Clutch's parser libraries are vendored,
so building Clutch requires no dependency downloads.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is `build/clutch` on Linux/macOS, `build/clutch.exe` with Windows
single-configuration generators, or `build/Release/clutch.exe` with Visual Studio.
Add its directory to `PATH`.

Your projects also need a C/C++ compiler and the selected native build tool;
Clutch selects an available compatible generator when omitted, preferring Ninja.
See [installation](docs/installation.md) for platform setup,
compiler environments and installation prefixes. Published downloads are listed
on [GitHub Releases](https://github.com/Pranav-32/Clutch/releases).

## Quick start

With Clutch and Ninja on `PATH`:

```sh
clutch new hello
cd hello
clutch .
clutch build
clutch run
```

`clutch .` reads the manifest and writes configuration into `Clutch-build-debug/`.
`clutch build` compiles and links the application; `clutch run` builds if needed
and starts it. To use the separate release profile:

```sh
clutch . --release
clutch run --release
```

Configure again after changing manifests or adding/removing source files.
Existing source and header edits only need another build.

Use `clutch build --clean` to rebuild selected targets and their dependencies,
or `clutch run --clean` to rebuild before running. Configuration and downloaded
sources are retained. Use `clutch outdated` to check compatible Git dependency
updates without changing your lock; see [dependency updates](docs/dependencies.md#locks-cache-and-offline-use).

## A simple manifest

For a project with `src/main.cpp`:

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

Add source files to the array for a multi-file executable, or omit both sources
and entry to discover files recursively from the manifest directory, filtered by
the project's language. Omit `generator` to select an available compatible build
tool automatically. `*.cpp` selects files beside the manifest; `**.cpp` includes
subdirectories. Choose compilers during configuration with
`clutch . --cc clang --cxx clang++`. Run `clutch generators` for generator choices
and `clutch --help` for commands. Use `clutch . --targets` to discover project
and CMake dependency library targets before selecting an import.

The build directory contains `compile_commands.json`; see
[IDE setup](docs/usage.md#build-directories-and-ides) for using it.

## Learn more

| I want to… | Guide |
| --- | --- |
| Configure, build, run or test a project | [Using Clutch](docs/usage.md) |
| Look up a manifest field | [Manifest reference](docs/manifest.md) |
| Import a local, Git or installed library | [Dependencies](docs/dependencies.md) and [package examples](examples/dependencies/README.md) |
| Organize multiple projects | [Workspaces](docs/workspaces.md) |
| Build an existing CMake application | [CMake project workflows](docs/cmake-projects.md) |
| Understand dependency pins | [Lock files](docs/lock-files.md) |
| Try a complete project | [Examples](examples/README.md) |
| Integrate an IDE or developer tool | [File API](docs/file-api.md) |
| Share sources and resolved source dependencies | [Source bundles](docs/usage.md#source-bundles) |

## Feature status

This table compares the checkout with the repository's `v0.3.2` source tag.
See the [changelog](docs/CHANGELOG.md) for release-specific changes.

| Availability | Features |
| --- | --- |
| In `v0.3.2` and this checkout | C/C++ executable/library/test targets; native generators and `--jobs`; separate profiles; workspaces and local locks; compilation databases, DOT graphs and the native file API |
| In this checkout, beyond `v0.3.2` | Flexible application/member manifests; recursive sources; Git resolution, recursive submodules and update reports; local/installed imports; CMake/Make library adapters with native preparation; complete CMake project delegation with presets, tests, install and packaging; limited named source catalogs; PCH; parallel builds and `--clean`; relocatable source bundles |
| Not implemented | Translating arbitrary CMake functions into native manifests, arbitrary foreign build systems/vcpkg recipes, CUDA device compilation, GUI telemetry and self-hosting |

## Contribute

Documentation, examples, tests and bug fixes are welcome. Start with the
[contributor guide](docs/CONTRIBUTING.md) for setup, code organization, conventions,
relevant checks and your first PR. Follow the [code of conduct](.github/CODE_OF_CONDUCT.md).

Report vulnerabilities privately through the [security policy](.github/SECURITY.md).

## License

Clutch uses the [BSD 3-Clause license](LICENSE).
Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors.
See [licensing](docs/licensing.md) for redistribution requirements.
