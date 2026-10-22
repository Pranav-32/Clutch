# Examples

To share an example as a relocatable source ZIP, run
`clutch bundle --project examples/multi-file --output multi-file-sources.zip`
from the checkout root. Extract it elsewhere, configure with `clutch .`, then
build with `clutch build`. See [source bundles](../docs/usage.md#source-bundles)
for dependency snapshots, exclusions and installed-tool prerequisites.

Build or install Clutch first, then make its executable available on `PATH`.
The application examples use Ninja and installed C/C++ compilers by default.

| Directory                              | Demonstrates                                          |
|----------------------------------------|-------------------------------------------------------|
| [single-file](single-file/clutch.toml) | One C++ entry source                                  |
| [multi-file](multi-file/clutch.toml)   | Multiple sources, arguments and executable tests      |
| [c-project](c-project/clutch.toml)     | C11 application                                       |
| [mixed](mixed/clutch.toml)             | Recursive C/C++ suffix selection with separate standards |
| [foreign](foreign/clutch.toml) | Local CMake library with its original native targets |
| [cmake-project](cmake-project/README.md) | Existing CMake application, generated resources, tests and packaging |
| [dependencies](dependencies/README.md) | Local directories, Git/GitHub imports, source catalogs and installed SDKs |
| [pch](pch/clutch.toml)                 | Shared prefix compilation and independent source builds |
| [targets](targets/README.md)           | Local libraries, public requirements and linked tests |
| [workspace](workspace/README.md)       | Member manifests, qualified links and a portable lock |
| [flexible](flexible/README.md)         | Root application, libraries, members and compile overrides |
| [file-api](file-api/README.md)         | Developer-tool client for the native file API         |

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
