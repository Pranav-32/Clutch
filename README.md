# Clutch

A small native build system for C and C++ with a simple `clutch.toml` manifest.
Configure your project, then build, run and test it from the command line.
Clutch generates native build files and an IDE compilation database.

**Status: early development.** Clutch currently builds one application executable
and independent test executables. Git dependencies, external-library build-system
adapters and self-hosting are planned; full CMake feature coverage is future work.

## What you can do

- Build C, C++ and mixed-language projects with explicit language standards.
- Use Ninja, GNU Make, NMake, JOM, Visual Studio/MSBuild or Xcode, depending on
  your operating system and installed tools.
- Keep debug and release builds separate, with simple optimization and warning
  settings in each profile.
- Generate `compile_commands.json` in the build directory for IDEs and language
  servers, alongside a Graphviz build graph.
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

## Learn more

| Guide | What it covers |
| --- | --- |
| [Installation](docs/installation.md) | Build and install Clutch on your platform |
| [Runnable examples](docs/examples.md) | Try single-file, multi-file, C and mixed-language projects |
| [Using Clutch](docs/usage.md) | Configuration, compilers, generators, IDE setup and project tests |
| [Manifest reference](docs/manifest.md) | Every supported field, default and example |

## Contribute

See [Contributing](docs/CONTRIBUTING.md) for development setup, code conventions,
formatting and review checks. The [development-container guide](docs/development-container.md)
provides a shared Docker environment for Linux, macOS and Windows hosts.
Implementation details live in the [architecture guide](docs/architecture.md).

## License

Clutch is licensed under [BSD 3-Clause](LICENSE).
Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors.
See [licensing](docs/licensing.md) for redistribution requirements and
third-party notices.
