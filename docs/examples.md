# Runnable examples

The [examples directory](../examples/) contains complete projects you can
configure and build with the Clutch executable. Each project is independent:
its `clutch.toml` describes its own sources, standards and build profiles.

| Example | Demonstrates | Expected application output |
| --- | --- | --- |
| [Single-file C++](../examples/single-file/clutch.toml) | C++17, a root-level entry source and minimal configuration | `Hello from Clutch and C++!` |
| [Multi-file C++](../examples/multi-file/clutch.toml) | Explicit source lists, colocated headers, runtime arguments and an executable test | `Hello, Clutch!` |
| [C project](../examples/c-project/clutch.toml) | C11 compilation and linking | `Hello from Clutch and C!` |
| [Mixed C/C++](../examples/mixed/clutch.toml) | Recursive source discovery, separate language standards and a C interface called from C++ | `Twice 21 is 42.` |

## Prerequisites

Build Clutch with CMake using the [installation guide](installation.md). Have
Ninja 1.10+ and the appropriate C/C++ compilers available. All example manifests
select Ninja by default. Clutch resolves installed compilers automatically, or
you can select them explicitly during configuration.

Run the commands below from the repository root. They use `clutch` on `PATH`.
You can instead invoke a locally built binary directly, without installing or
copying it into the example:

Linux or macOS, with the CMake build in `build/`:

```sh
./build/clutch examples/single-file
./build/clutch run --project examples/single-file
```

PowerShell, with a Visual Studio Release build in `build/`:

```powershell
& .\build\Release\clutch.exe examples/single-file
& .\build\Release\clutch.exe run --project examples/single-file
```

Replace the executable path with your actual build output, such as
`build-container/clutch`, `build-msvc/Release/clutch.exe`, or `build/clutch.exe`
for a Windows single-configuration build. On Windows, use an initialized
Visual Studio developer terminal when selecting Microsoft compiler/SDK tools.
The source examples are checked in; generated binaries stay in build directories.

## Configure, build and run

Start with the single-file C++ application:

```sh
clutch examples/single-file
clutch build --project examples/single-file
clutch run --project examples/single-file
```

Configuration reads that directory's manifest and writes native build files and
`compile_commands.json` into `examples/single-file/Clutch-build-debug/`.
It does not compile. `build` compiles and links; `run` builds when needed and
launches the executable. Clutch's status messages accompany the application output.

Run the other examples with the same workflow:

```sh
clutch examples/c-project
clutch run --project examples/c-project

clutch examples/mixed
clutch run --project examples/mixed

clutch examples/multi-file
clutch run --project examples/multi-file
clutch run --project examples/multi-file -- "Clutch contributor"
```

The final command forwards one argument containing a space and prints
`Hello, Clutch contributor!`.

## Build and run tests

The multi-file project declares a `greeting` test with its own `main`. Both the
application and test explicitly compile `src/greeting.cpp`; the application
entry point is excluded from the test source list.

```sh
clutch test --project examples/multi-file
clutch test --project examples/multi-file --filter greeting
```

Successful execution prints `Greeting tests passed.` and returns zero.
The test uses explicit checks so it continues to validate behavior in release
builds too.

## Select a profile, compiler or generator

Each manifest declares separate debug and release directories. For example:

```sh
clutch examples/multi-file --release
clutch run --project examples/multi-file --release
clutch test --project examples/multi-file --release
```

Choose compilers or a different generator during configuration:

```sh
clutch examples/multi-file --cc clang --cxx clang++
clutch examples/multi-file --generator make
clutch generators
```

Use tools supported by your host; see the [generator requirements](usage.md#generators).
The `generator` field in each manifest is the persistent default; a CLI override
applies to that configuration. `build`, `run` and `test` consume the saved
configuration. For independent compiler/generator builds, assign distinct
`build_dir` paths in the manifest profiles or work from separate copies.

The Clutch development container provides Linux compilers and executors. Keep
container builds separate from native Windows/macOS builds because saved tool
paths and binaries are host-specific.

## Edit, inspect and clean

Edit an existing source or header, then build again to exercise incremental
compilation. After changing a manifest or adding/removing source files, rerun
configuration. Point your IDE at the selected example's profile directory to
read its `compile_commands.json`.

Clean a selected example profile with:

```sh
clutch clean --project examples/multi-file
clutch clean --project examples/multi-file --release
```

Cleanup removes the selected owned build directory, including its configuration;
configure again before building. Example profile outputs are ignored by Git.
Copy an example to start your own project, keeping the manifest and source layout
and changing its project name as needed. The repository root builds Clutch itself
with CMake; it has no `clutch.toml`.
