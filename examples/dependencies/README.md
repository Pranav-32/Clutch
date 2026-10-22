# Package dependency examples

Build/install Clutch and put it on `PATH`. These are runnable manifests, with a
shared application and an independent `package-test` executable. Keep this whole
directory together: the nested manifests reference `../main.cpp` and `../checks/`.
Each manifest selects its own include path for `clutch_example.hpp`, keeping the
package-specific API calls next to the corresponding manifest.

| Project | Source | Requirements |
| --- | --- | --- |
| [Default](clutch.toml) | Live local header directory, native Clutch library, installed-package prefix | C++17 compiler, Ninja, CMake 3.21+ |
| [git-local](git-local/clutch.toml) | Local Git repository pinned at `v1.0.0` | Git 2.29+; run preparation below |
| [github-headers](github-headers/clutch.toml) | CLI11 from GitHub, header-only | Git; network for first configuration |
| [git-cmake](git-cmake/clutch.toml) | fmt from GitHub using its original CMake target | Git, CMake 3.21+; network for first configuration |
| [makefile](makefile/clutch.toml) | Local library through its GNU Make rules | GNU Make, GCC/Clang-style compiler and `ar` (POSIX sample) |
| [system-interface](system-interface/clutch.toml) | Explicit installed headers without package metadata | C++17 compiler and Ninja; no CMake or Git needed |
| [catalog](catalog/clutch.toml.in) | Named port from an isolated source catalog | Git; preparation generates `catalog/clutch.toml`; no vcpkg executable |
| [directx](directx/clutch.toml) | Machine-level Windows SDK, `d3d12` import library | Windows SDK and initialized MSVC/clang-cl developer terminal |
| [cuda](cuda/clutch.toml) | Machine-level CUDA Toolkit, static runtime target | Compatible installed CUDA Toolkit and CMake 3.21+ |

## Local directories and installed packages

From the repository root:

```sh
clutch examples/dependencies
clutch run --project examples/dependencies
clutch test --project examples/dependencies
```

The application prints `Local packages: 42; installed SDK: 1.0.0`.
`local/headers/` has no build manifest. `local/number/` supplies a compiled native
library. Both are live `path` imports; Clutch reads them in place. Their public
include paths and the library's link dependency are automatic.

`installed-sdk/` is a small **example installation prefix**, containing headers
and a CMake package config. It exercises the `system` provider without changing
your machine. A real installation can be selected with its package name, imported
target and optional `root` prefix. `root` locates existing files; it installs
nothing and does not change global environment variables.

For an SDK without CMake metadata, run `clutch examples/dependencies/system-interface`
and the corresponding `clutch run --project ...` / `clutch test --project ...` commands.
Its arbitrary `system = "vendor-sdk"` identity supplies explicit `include_dirs`.
Compiled SDKs can add `libraries` containing linker base names or existing file
paths. Clutch has no built-in vendor/package-name list.

The [Makefile variant](makefile/clutch.toml) explicitly chooses the native library's
alternative GNU Make rules and declares its output archive/include directory.
Its `link_language = "cpp"` also declares the library's link-driver requirement;
a C-only consumer can use the same field without compiling its sources as C++.
Run it the same way on a POSIX host with GNU Make and `ar`. Its objects and archive
stay in the profile's foreign-build sandbox; the source directory is not modified.

## Local Git repositories and named catalogs

From the repository root:

```sh
python examples/dependencies/prepare.py
clutch examples/dependencies/git-local
clutch run --project examples/dependencies/git-local
clutch test --project examples/dependencies/git-local

clutch examples/dependencies/catalog
clutch run --project examples/dependencies/catalog
clutch test --project examples/dependencies/catalog
```

Use `python3` if that is your Python command. The preparation helper requires
Python 3.9+ and Git only to create these demonstration repositories. **Clutch does
not require Python.** An existing local Git repository can be used directly in a
`git` field without this helper.

The helper copies the native library into `Clutch-build-fixtures/repositories/`,
publishes a local Git tag, and creates a small catalog with a literal upstream
Git declaration. It substitutes the full catalog commit into the generated
`catalog/clutch.toml`. It does not download packages, install vcpkg, change global
Git configuration or overwrite edited/unowned outputs. Repeating preparation
reuses the clean published repositories and keeps the same baseline.

`path` consumes live files; `git` consumes a verified commit in the dependency
cache. Editing `local/number/` affects the local-directory example, not a Git
package already pinned to its published commit. Local-origin locks contain
machine-specific absolute paths, so these two examples ignore their generated
lock files. Real shared projects should use hosted repository URLs and commit
their portable locks.

The named catalog demonstrates the supported `vcpkg` source-metadata format.
It is not an installer or an example of general vcpkg recipe compatibility;
see the [catalog restrictions](../../docs/dependencies.md#optional-named-source-catalog).

## GitHub and other Git servers

```sh
clutch examples/dependencies/github-headers
clutch run --project examples/dependencies/github-headers -- --value 7
clutch test --project examples/dependencies/github-headers

clutch examples/dependencies/git-cmake
clutch run --project examples/dependencies/git-cmake
clutch test --project examples/dependencies/git-cmake
```

The header-only example fetches [CLI11 v2.4.2](https://github.com/CLIUtils/CLI11/tree/v2.4.2)
and uses its real option parser. The CMake example fetches
[fmt 11.0.2](https://github.com/fmtlib/fmt/tree/11.0.2) and links `fmt::fmt`.
Clutch owns the application graph; fmt keeps its original upstream build rules.
These are deliberately pinned examples, not claims about the latest releases.

GitHub is an ordinary Git source to Clutch. For another server, change `git` to
that repository's HTTPS or SSH URL and select its tag, branch, commit or version
constraint. Use Git's normal credential helpers for private repositories.
Downloads stay in each project's `.clutch/` cache. After the first successful
configuration, verify cached use with:

```sh
clutch examples/dependencies/github-headers --locked --offline
clutch examples/dependencies/git-cmake --locked --offline
```

Commit the remote examples' `clutch.lock` files when adapting them to a real
project. To update an exact pin, edit its tag/revision. Untagged imports and
version ranges can be refreshed with `--auto-update-dependencies`; the normal
configure preserves compatible locked revisions.

## Machine-level SDKs

In an initialized Windows SDK developer terminal:

```powershell
clutch examples/dependencies/directx
clutch run --project examples/dependencies/directx
clutch test --project examples/dependencies/directx
```

With an installed CUDA Toolkit discoverable by CMake:

```sh
clutch examples/dependencies/cuda
clutch run --project examples/dependencies/cuda
clutch test --project examples/dependencies/cuda
```

These programs retain real SDK symbol references without creating a GPU device,
opening a window or running a kernel. The CUDA example compiles ordinary C++
and uses `CUDA::cudart_static` to avoid guessing runtime DLL names. `.cu`
compilation and device linking are not implemented. Installed SDK contents and
ABI are machine-local; a lock does not freeze them. Clutch installs neither SDK.
The DirectX manifest uses the generic explicit-library interface:
`system = "windows-sdk", libraries = ["d3d12"]`. That name has no special meaning
to Clutch. The CUDA manifest uses ordinary CMake package discovery. Other SDKs
use the same two mechanisms.

## Profiles and tests

All examples define debug and release directories in their manifests. For any
project above, add `--release` to configure, run and test to select release.
Use `--cc`, `--cxx` or `--generator` during configuration for other host tools.

`clutch.package-examples` runs the checked-in local, installed-prefix, Git and
catalog examples in both profiles and verifies locked offline configuration.
Windows MSVC/clang-cl environments also exercise the real DirectX example.
The GitHub smoke tests require the test runner's explicit `--network` option;
CUDA requires `--cuda`. See [testing](../../docs/testing.md#package-examples)
for the commands. These flags belong to the test runner, not Clutch.
