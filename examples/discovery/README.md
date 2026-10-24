# Automatic, flat and recursive source selection

Requires C11 and C++17 compilers and a compatible build executor. Keep this
directory together: the flat/recursive variants share `src/`.

From the repository root:

```sh
clutch examples/discovery
clutch run --project examples/discovery
clutch examples/discovery/flat
clutch run --project examples/discovery/flat
clutch examples/discovery/recursive
clutch run --project examples/discovery/recursive
```

Each application prints `Discovered: 42`. The root manifest omits entry, sources
and generator, so Clutch discovers language-appropriate files and an available
compatible executor. It compiles four source units; headers remain IDE files.
The flat variant compiles three units: `*` stays in its directory, so the deeper
`extra.cpp` is omitted. The recursive variant uses `**` and braces to include all
four units. Compare each profile's `compile_commands.json`.

Explicit patterns resolve from the declaring manifest, including `../src/` here.
Use `language = "c"` or `"cpp"` for single-language discovery. For mixed projects,
the `std` table supplies each language's standard. Reconfigure after changing
membership, or use `clutch watch`. See [source selection](../../docs/manifest.md#sources-and-headers).
