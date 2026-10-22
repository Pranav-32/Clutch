# Existing CMake application

This application keeps its original CMake project. A custom command generates a
header, a helper creates the executable target, CTest runs it and CPack provides
ZIP packaging. Clutch delegates those rules rather than recreating them.

With Clutch, CMake 3.21+, a C++17 compiler and Ninja installed:

```sh
clutch examples/cmake-project --generator Ninja
clutch targets --project examples/cmake-project
clutch run --project examples/cmake-project --target hello
clutch test --project examples/cmake-project
clutch package --project examples/cmake-project
```

Build output and IDE metadata stay under `Clutch-build-debug/`. Try changing
`message.in` and running again to exercise the original generated-header rule.
Use `--release` on both configuration and subsequent commands for a release
build. No `clutch.toml` is needed. See the
[provider guide](../../docs/cmake-projects.md) for presets, installation and IDEs.
