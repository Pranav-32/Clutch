# Profiles, warnings and debug symbols

Requires Clutch, a C++17 compiler and a compatible executor. Keep the no-symbols
variant with its parent because it shares `main.cpp`.

```sh
clutch .
clutch run
clutch . --release
clutch run --release
clutch debug --release
clutch lint --release
```

Both profiles print `Profile example: 42` and retain symbols. Debug is unoptimized;
release is optimized for speed. The release warning array enables shadow warnings
then disables that category, showing ordered overrides. It also treats remaining
warnings as errors. Strict release safety promotes selected lint diagnostics, but
does not enable a sanitizer or lifetime analysis by itself.

For debug/release without symbols, configure `no-symbols/` and run with
`clutch run --project no-symbols` (add `--release` after release configuration).
Its debug warnings use `"none"`; release uses `"default"` and optimizes for size.
`clutch debug --project no-symbols` rejects the missing-symbol policy. Ordinary
`"extra"` presets appear in the [single-file example](../single-file/clutch.toml).

Inspect the compilation database for the selected compiler's mapped flags.
MSVC-style and Microsoft-ABI Clang builds produce linker PDBs when symbols are
enabled; common GCC/Unix Clang setups embed debug information instead. Formatting,
linting and debugging need their selected optional tools. See [profile fields](../../docs/manifest.md#profiledebug-and-profilerelease),
[warning arrays](../../docs/manifest.md#warning-arrays) and [safety levels](../../docs/manifest.md#safety-strictness-levels).
