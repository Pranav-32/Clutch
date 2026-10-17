# Static analysis

The root [`.clang-tidy`](../.clang-tidy) provides an advisory analysis baseline for
Clutch's own C++17 implementation. It is independent of `.clang-format`:
clang-format controls layout, while clang-tidy examines code using the compiler's
types and compilation settings. Neither file is installed into projects built by
Clutch.

## Policy

The configuration enables:

- Clang analyzer checks for invalid memory access, lifetime mistakes, undefined
  operations, dead stores, and selected security and C-library misuse.
- `bugprone-*` checks for suspicious constructs, including dangling references,
  incorrect string operations, narrowing conversions, and use after move.
- `performance-*` checks for unnecessary copies and inefficient operations.
- Portability checks that do not impose an include-layout convention.
- `misc-const-correctness` for immutable local values, pointees, and pointer bindings.
- `modernize-use-auto` and `modernize-use-ctad` for safe type deduction; explicit
  element types remain necessary for empty containers and typed parser APIs.
- Pointer-qualified `auto`, redundant declarations, and redundant top-level
  `const` in function declarations are checked by the selected readability rules.
- Selected modernization checks for `nullptr`, `override`, type aliases, and
  explicitly defaulted or deleted functions, plus redundant-control-flow cleanup.

Findings remain warnings: `WarningsAsErrors` is empty. Compiler errors still fail
analysis, and a caller can explicitly override this warning policy. Newer LLVM
releases may add checks to wildcard groups; review newly reported findings when
upgrading LLVM.

The following exclusions are deliberate:

- `bugprone-easily-swappable-parameters` can produce excessive noise for functions
  accepting several paths, strings, or compiler arguments.
- `performance-enum-size` can suggest changing representation for negligible
  benefit and should not establish an ABI policy automatically.
- `portability-avoid-pragma-once` would conflict with the project's header style.
- `portability-restrict-system-includes` needs a platform-specific include policy
  that this cross-platform tool does not impose.

Header diagnostics are restricted to the co-located project headers under `src/`.
Vendored headers under `third_party/`, generated build directories, and system
headers are outside that filter. A source passed directly to clang-tidy is always
diagnosed, so select only owned `.cpp` files when running an analysis. A header
filter affects displayed diagnostics, not which included headers must be parsed.

Use structured bindings where they clarify pairs or tuples, `constexpr` for
appropriate compile-time values, and blank lines between meaningful code blocks.
These remain review guidelines; the baseline does not claim to enforce them all.
It also avoids a blanket identifier naming policy and checks requiring a language
standard newer than C++17.

## Analyze using a CMake build directory

Use an out-of-source Ninja configuration with compilation database export enabled.
Clutch's normal CMake build does not need database export. This separate analysis
configuration keeps `compile_commands.json` inside its build directory:

```powershell
cmake -S . -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build-tidy src/main.cpp
```

Run from a shell where the chosen compiler, Ninja, and its SDK are available. For
MSVC on Windows, use a Visual Studio developer shell. Existing project CMake
options can be passed to this configure command as usual. No CMake build is needed
unless a translation unit requires generated headers or other preparation.

Analyze all owned implementation files in PowerShell:

```powershell
$sourceFiles = Get-ChildItem -LiteralPath src -Recurse -File -Filter *.cpp
foreach ($sourceFile in $sourceFiles) {
    clang-tidy -p build-tidy $sourceFile.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "clang-tidy failed for $($sourceFile.FullName)"
    }
}
```

On a POSIX shell, an equivalent source selection is:

```sh
find src -name '*.cpp' -exec clang-tidy -p build-tidy {} +
```

Do not copy or generate a compilation database into the repository root. When an
IDE runs clang-tidy, select the CMake profile or build-directory database for that
IDE so the analyzer receives the actual include paths, definitions, and standard.
The root configuration is discovered through each source file's parent folders.
An IDE can also use a different compatible clang-tidy executable; check its
configuration support before enabling it across the project.

For projects built with Clutch, configure explicitly with `clutch .` (or
`clutch path/to/project`). Their `compile_commands.json` lives inside the selected
profile's `build_dir`, independently of generator selection. Point clang-tidy or
the IDE at that directory. After changing the manifest or discovered source set,
configure again before analyzing; `clutch build` consumes the saved graph and
does not reconfigure. These databases are separate from the CMake analysis
database used to develop Clutch itself.

The [contributor container](development-container.md) includes LLVM 22 tools.
Its optional CMake database stays under `build-container`; enable export there
when needed. See [Contributing](CONTRIBUTING.md) for style and validation.
See the [testing guide](testing.md#scope-and-validation-status) for build/test
validation status; those checks do not establish a clean clang-tidy analysis.

## Check configuration and review fixes

The baseline was checked against LLVM clang-tidy 22.1.6. Inspect the local tool's
supported checks and validate the configuration without analyzing sources:

```powershell
clang-tidy --version
clang-tidy --config-file=.clang-tidy --verify-config
clang-tidy --config-file=.clang-tidy --list-checks
```

These validation commands do not require a compilation database. For older LLVM
versions, consult `clang-tidy --help` if `--verify-config` is unavailable; do not
silently ignore unsupported check or option warnings.

Run analysis without `--fix` first. Review findings against platform-specific code
and public behavior before editing. `FormatStyle: file` only chooses formatting
when fixes are explicitly requested; it does not enable fixes. If a particular
finding is intentionally suppressed, use the narrowest `NOLINT` scope with its
check name and a reason.
