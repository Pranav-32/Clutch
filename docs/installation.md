# Installing Clutch

Build Clutch from a source checkout with CMake 3.20 or newer and a C++17
compiler. CLI11 2.4.2 and toml++ 3.4.0 are included in `third_party/`, so building
Clutch requires no dependency downloads. See the
[dependency notes](../third_party/README.md) for versions and licenses.

Projects built with Clutch also need a C/C++ compiler and the native build tool
selected by their generator. Ninja is the default; see the
[generator requirements](usage.md#generators) for alternatives.

Installation includes Clutch's license and the vendored-library notices under
`share/licenses/clutch/`. Keep these with binary redistributions; see
[licensing](licensing.md) for the requirements.

## Linux and macOS

Install CMake and a C++17 compiler such as GCC or Clang; on macOS, install Apple's
command-line development tools. Install Ninja to follow the quick-start workflow.
From the checkout root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/clutch --version
```

To install into your user-local prefix:

```sh
cmake --install build --config Release --prefix "$HOME/.local"
```

Add `$HOME/.local/bin` to your shell's `PATH`, then check `clutch --version`.
The executable uses the host's standard system/runtime libraries.

## Windows

Install CMake and a C++17 compiler. For MSVC, install Visual Studio or Build Tools
with the **Desktop development with C++** workload and a Windows SDK. Use
`cmake --help` to find the generator name matching your installed version.
For Visual Studio 2022, for example:

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
.\build-msvc\Release\clutch.exe --version
cmake --install build-msvc --config Release --prefix "$env:LOCALAPPDATA\Clutch"
```

Add `%LOCALAPPDATA%\Clutch\bin` to your user `PATH` and open a new terminal.
Clutch built with MSVC uses the static MSVC runtime; Windows system DLLs remain
required. MinGW-built executables need their runtime DLLs to be discoverable,
for example through the launching terminal's MinGW `bin` directory on `PATH`.

When using Clutch with MSVC, NMake, or Clang targeting the Microsoft SDK, run it
from an initialized Visual Studio developer terminal. Clutch inherits the
compiler, linker and SDK environment without changing global settings.

## Select the compiler used to build Clutch

CMake selects the compiler for Clutch itself. For a GCC or Clang build, choose
it on the initial configuration, using a separate directory for each compiler:

```sh
cmake -S . -B build-clang -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang
```

In CLion, add the desired toolchain under **Settings > Build, Execution,
Deployment > Toolchains**, select it in a CMake profile, and give the profile its
own build directory. Keep native and container build trees separate.

Projects configured by Clutch select their own generator and compilers
independently; see [compiler selection](usage.md#configure-then-build).
Self-hosting remains deferred: build Clutch itself with CMake. Runnable Clutch
projects live under [examples/](../examples/README.md), each with its own manifest.
The normal CMake build does not publish a compilation database to the
repository root.

## Verify or contribute

Once installed, configure a standalone project or a [workspace](workspaces.md)
using the same native executable. Configuration generates a portable
[`clutch.lock`](lock-files.md); the first configuration needs permission to create
it in the source root. Existing matching locks support `--locked` verification.
Workspace and lock handling require no Python runtime.

Use `clutch --version` and the [quick start](../README.md#quick-start) to check
your installation. To validate a development checkout, follow the
[testing guide](testing.md). A shared Docker environment is described in the
[development-container guide](development-container.md); contributor conventions
and review expectations are in [Contributing](CONTRIBUTING.md).
