# Roadmap

Audience: Users, contributors and tool authors who want to know where Clutch is
heading. This page describes direction, not commitments: it has no dates, and a
planned item is not supported until its section says so and the
[changelog](CHANGELOG.md) lists it.

## Direction

Clutch aims to be a standalone native build system for C, C++, Objective-C and
CUDA. It should configure and build projects itself, adapt to libraries that use
other build systems, build its own future versions, and over time become a
practical alternative to CMake for new and migrating projects.

## Status of each area

| Area | Status |
| --- | --- |
| C and C++ executables, libraries and tests; native generators; profiles; workspaces; Git dependencies and locks; file API; developer tools; installers | Available |
| Adapting CMake and Makefile libraries; delegating complete CMake projects | Available, with the limits in [dependencies](dependencies.md) and [CMake projects](cmake-projects.md) |
| Building Clutch with Clutch | Available from a CMake-built bootstrap; see [native self-hosting](installation.md#build-clutch-with-clutch) |
| Forward-compatible manifests and an older release building newer sources | Planned (next) |
| Per-language model, C++26 and GNU language dialects | Planned |
| Objective-C and Objective-C++ | Planned |
| C++20 modules | Planned |
| CUDA | Planned |
| More foreign build systems and exporting Clutch libraries to CMake | Planned |
| Features needed to replace CMake in larger projects | Planned |

## Planned work, in order

**1. Forward compatibility and self-hosting.** Manifests reject unknown keys,
so a manifest that uses a newer feature currently fails on an older Clutch with
an unclear message. A manifest will be able to declare the oldest Clutch
version it needs, and older versions will report that requirement clearly.
Clutch's own manifest will use only features of the previous release, checked
in CI by building each change with the previous published release and then
rebuilding it with the result.

**2. Language model and standards.** Source files will carry an explicit
language instead of the current C/C++ split, with per-language compilers,
standards and precompiled headers. C++26 will be accepted with compiler-specific
flags, and projects will be able to opt into GNU language dialects.

**3. Objective-C and Objective-C++.** `.m` and `.mm` sources with Clang and
GCC, Apple framework linking and native Xcode file types. Hosts without an
Objective-C toolchain will reject these sources clearly.

**4. C++20 modules.** Module interface sources, dependency scanning with each
compiler's P1689 support, correctly ordered builds through Ninja's dynamic
dependencies, and module information in `compile_commands.json` and the file
API so editors understand imports. Visual Studio will use its native module
support. Other generators will reject modules clearly until they can order them.

**5. CUDA.** `.cu` sources through `nvcc` or Clang, CUDA toolkit discovery,
GPU architecture selection, separable compilation and runtime linking.

**6. More build systems.** A common adapter interface for foreign libraries,
starting with Meson and Autotools, and exporting Clutch libraries as CMake and
pkg-config packages so other build systems can consume them.

**7. Replacing CMake in larger projects.** Install and export rules, packaging,
cross-compilation profiles, named configurations, feature options, native
pkg-config discovery, and generating a `clutch.toml` from an existing CMake
project's metadata.

Machine-readable build output and other editor-integration improvements will
be added alongside these areas. See [contributing](CONTRIBUTING.md) to help.
