# Examples

To share an example as a relocatable source ZIP, run
`clutch bundle --project examples/multi-file --output multi-file-sources.zip`
from the checkout root. Extract it elsewhere, configure with `clutch .`, then
build with `clutch build`. See [source bundles](../docs/usage.md#source-bundles)
for dependency snapshots, exclusions and installed-tool prerequisites.

Build or install Clutch first, then make its executable available on `PATH`.
Most application examples select Ninja; examples omitting a generator discover
an available compatible executor. Optional tools and platform prerequisites are
listed in each example's README. Unimplemented features have no runnable sample.

| Directory                              | Demonstrates                                          |
|----------------------------------------|-------------------------------------------------------|
| [single-file](single-file/clutch.toml) | One C++ entry source                                  |
| [multi-file](multi-file/clutch.toml)   | Multiple sources, arguments and executable tests      |
| [c-project](c-project/clutch.toml)     | C11 application                                       |
| [mixed](mixed/clutch.toml)             | Recursive C/C++ suffix selection with separate standards |
| [discovery](discovery/README.md) | Automatic source/generator discovery; flat and recursive mixed-language patterns |
| [foreign](foreign/clutch.toml) | Local CMake library with its original native targets |
| [cmake-project](cmake-project/README.md) | Existing CMake application, generated resources, tests and packaging |
| [dependencies](dependencies/README.md) | Local directories, Git/GitHub imports, source catalogs and installed SDKs |
| [pch](pch/clutch.toml)                 | Shared prefix compilation and independent source builds |
| [tooling](tooling/README.md) | Formatting, analysis, debugging, sanitizer requests and optimized debug symbols |
| [profiles](profiles/README.md) | Warning presets/arrays, independent optimization/symbols and normal/strict policy |
| [safety](safety/README.md) | Disabled, address, undefined, leak, thread, combined and lifetime-check policies |
| [configured-files](configured-files/README.md) | Version files, Git identity, literal variables and generated includes |
| [commands](commands/README.md) | Named scripts and before/after configure/build hooks |
| [watch](watch/README.md) | Build/run/test watch loops, resources, exclusions, debounce and failure recovery |
| [targets](targets/README.md)           | Local libraries, public requirements and linked tests |
| [shared-library](shared-library/README.md) | Shared-library exports, public headers, separate tests and runtime staging |
| [workspace](workspace/README.md)       | Member manifests, qualified links and a portable lock |
| [flexible](flexible/README.md)         | Root application, libraries, members and compile overrides |
| [file-api](file-api/README.md)         | Developer-tool client for the native file API         |
| [generators](generators/README.md) | Ninja, Make, NMake, JOM, MSBuild and Xcode compiler/generator recipes |
| [bundle](bundle/README.md) | Exclude generated resources, extract a source ZIP and rebuild elsewhere |

From the repository root:

```sh
clutch examples/multi-file
clutch run --project examples/multi-file
clutch run --project examples/multi-file -- "Clutch contributor"
clutch test --project examples/multi-file
clutch examples/multi-file --release
clutch run --project examples/multi-file --release
```

Alternatively, change into an example directory and use `clutch .`, `clutch run`
and `clutch test`. The workspace example runs from its root and uses qualified
target names as shown in its README.

Use `--cc`/`--cxx` or `--generator` during configuration to choose different
installed tools. Generated files stay in the manifest's profile directories;
`clutch clean --project examples/multi-file` removes the selected owned build.
Use separate build trees for different hosts. See [usage](../docs/usage.md), the
[manifest reference](../docs/manifest.md) and [installation](../docs/installation.md).

## Find a scenario

| Use case | Example or command |
| --- | --- |
| One C++ file; multiple C++ files; C; mixed C/C++ | `single-file`, `multi-file`, `c-project`, `mixed` |
| Omit sources/generator; select `*` versus `**`/braces | `discovery`, `discovery/flat`, `discovery/recursive` |
| Static/shared libraries, public/private requirements, executable tests | `targets`, `shared-library` |
| Multiple manifests; root application with members | `workspace`, `flexible` |
| Header-only/local/native/Git/GitHub dependencies | `dependencies`, `dependencies/git-local`, `dependencies/github-headers` |
| CMake/Make libraries and installed packages/SDKs | `foreign`, `dependencies/git-cmake`, `dependencies/makefile`, `dependencies/system-interface`, `dependencies/directx`, `dependencies/cuda` |
| Named source catalogs without installing vcpkg | `dependencies/catalog` after its documented preparation |
| Existing CMake app, custom generation, tests/install/packaging | `cmake-project` and the [CMake workflow guide](../docs/cmake-projects.md) |
| Git pins, update reports, locked/offline configuration | [Dependency workflow](dependencies/README.md#github-and-other-git-servers) |
| PCH and localized compiler include paths | `pch` and its prefix header |
| Debug/release, symbols, warnings and safety strictness | `profiles`, `profiles/no-symbols`, `safety` variants |
| Format, lint, debug | `tooling`; use its optional installed developer tools |
| Python/Node/native/shell commands and lifecycle hooks | `commands` and its tool variants |
| Watch sources/tests/resources or recover after a failed build | `watch` |
| Generated version/configuration text; Git-derived identity | `configured-files` |
| Compilation database and native IDE queries/replies | `file-api`; open generated MSBuild/Xcode projects using the generator recipes |
| Parallel builds; clean rebuild before run/tests | `clutch build --project targets --jobs 2`; from this directory use `clutch run --project shared-library --target hello --clean` after configuring it |
| Graph visualization | [Graph recipe](graphs/README.md) |
| Relocatable source bundle with exclusions | `bundle` recipe |
| Bootstrap Clutch, then build it natively | [Self-hosting instructions](../docs/installation.md#build-clutch-with-clutch); the root manifest builds the actual tool |

Commands using a short project name in this last table run from `examples/`.
Other command blocks state their working directory explicitly. Configure before
normal build/run/test; watch can configure a fresh project itself. For scaffolding,
run `clutch new hello` or `clutch init existing-project` in your own scratch
directory rather than replacing a checked-in example.
