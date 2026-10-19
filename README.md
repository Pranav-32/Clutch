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
Ninja is the default. See [installation](docs/installation.md) for platform setup,
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

Add source files to the array for a multi-file executable, or omit it to discover
sources under `src/`. Choose compilers during configuration with
`clutch . --cc clang --cxx clang++`. Run `clutch generators` for generator choices
and `clutch --help` for commands.

The build directory contains `compile_commands.json`; see
[IDE setup](docs/usage.md#build-directories-and-ides) for using it.

## Learn more

| I want to… | Guide |
| --- | --- |
| Configure, build, run or test a project | [Using Clutch](docs/usage.md) |
| Look up a manifest field | [Manifest reference](docs/manifest.md) |
| Import a local, Git or installed library | [Dependencies](docs/dependencies.md) and [package examples](examples/dependencies/README.md) |
| Organize multiple projects | [Workspaces](docs/workspaces.md) |
| Understand dependency pins | [Lock files](docs/lock-files.md) |
| Try a complete project | [Examples](examples/README.md) |
| Integrate an IDE or developer tool | [File API](docs/file-api.md) |

## Feature status

This table compares the checkout with the repository's `v0.3.2` source tag.
See the [changelog](docs/CHANGELOG.md) for release-specific changes.

| Availability | Features |
| --- | --- |
| In `v0.3.2` and this checkout | C/C++ executable/library/test targets; native generators and `--jobs`; separate profiles; workspaces and local locks; compilation databases, DOT graphs and the native file API |
| In this checkout, beyond `v0.3.2` | Flexible application/member manifests; recursive source patterns; Git resolution/updates; local/installed imports; CMake/Make adapters; limited named source catalogs; PCH and CPU-based default job limits |
| Not implemented | Full CMake feature coverage, arbitrary foreign build systems/vcpkg recipes, CUDA device compilation, self-hosting and portable bundles |

## Contribute

Documentation, examples, tests and bug fixes are welcome. Start with the
[contributor guide](docs/CONTRIBUTING.md) for setup, code organization, conventions,
relevant checks and your first PR. Follow the [code of conduct](.github/CODE_OF_CONDUCT.md).

Report vulnerabilities privately through the [security policy](.github/SECURITY.md).

## License

Clutch uses the [BSD 3-Clause license](LICENSE).
Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors.
See [licensing](docs/licensing.md) for redistribution requirements.
