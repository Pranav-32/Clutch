# Generator and compiler recipes

Use the existing [single-file project](../single-file/clutch.toml) to exercise
native generator formats without duplicating application sources. Run these
commands from the repository root. Install each named executor/compiler first.
Use one generator/compiler combination per profile directory; clean the owned
profile before changing generator formats, or work in a fresh example copy.

| Host/setup | Configuration command |
| --- | --- |
| Any supported host; automatic compatible selection | `clutch examples/discovery` |
| Ninja, GCC-style compiler flags | `clutch examples/single-file --generator ninja --cc gcc --cxx g++` |
| Ninja, Clang | `clutch examples/single-file --generator ninja --cc clang --cxx clang++` |
| GNU Make on Linux/macOS | `clutch examples/single-file --generator make --cc cc --cxx c++` |
| MinGW GCC on Windows | Same Ninja/GCC command in a terminal with the MinGW compiler on `PATH` |
| Ninja, MSVC | `clutch examples/single-file --generator ninja --cc cl --cxx cl` |
| Ninja, clang-cl | `clutch examples/single-file --generator ninja --cc clang-cl --cxx clang-cl` |
| Visual Studio/MSBuild | `clutch examples/single-file --generator visual-studio --cc cl --cxx cl` |
| NMake | `clutch examples/single-file --generator nmake --cc cl --cxx cl` |
| JOM | `clutch examples/single-file --generator jom --cc cl --cxx cl` |
| Xcode on macOS | `clutch examples/single-file --generator xcode --cc /usr/bin/clang --cxx /usr/bin/clang++` |

Microsoft ABI builds need an initialized Visual Studio developer environment
with the SDK/compiler libraries available. Xcode needs the full active Xcode
installation, not only command-line tools. GNU Make may be named `gmake` on
your host; pass `--build-tool gmake`. Executor paths with spaces can be quoted:

```sh
clutch examples/single-file --generator ninja --build-tool "/path with spaces/ninja"
clutch run --project examples/single-file
```

The path is illustrative; substitute the installed executable. Generator selection
belongs to configuration; saved builds use that selection. Visual Studio/Xcode
emit native project files, while command-line generators emit native build rules.
Every generator emits the profile's compilation database. See [generator requirements](../../docs/manifest.md#generators-and-compiler-selection)
for compatibility and [installation](../../docs/installation.md) for environments.
