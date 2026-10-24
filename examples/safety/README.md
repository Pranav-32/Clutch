# Safety policies and compiler/runtime support

Requires Clutch, a C++17 compiler and a compatible build executor. Keep this
directory together: all detector manifests share `main.cpp`. The programs are
safe and normally print `Safety example: 42`; they do not deliberately leak or
access invalid memory.

| Directory | Requested check | Additional requirements |
| --- | --- | --- |
| [Root](clutch.toml) | None; normal debug / strict release policy | No optional detector runtime |
| [address](address/clutch.toml) | Out-of-bounds/use-after-free instrumentation | Compatible ASan compiler/runtime; Windows Clang/MSVC DLLs are staged automatically |
| [undefined](undefined/clutch.toml) | Supported undefined-behavior checks | Compatible GNU-style compiler and UBSan runtime |
| [leak](leak/clutch.toml) | Runtime leak detection | Compatible LeakSanitizer host/runtime; unavailable on Windows |
| [thread](thread/clutch.toml) | Data-race instrumentation | Compatible POSIX ThreadSanitizer runtime and pthread library; runs two workers using an atomic counter |
| [combined](combined/clutch.toml) | Address plus undefined checks | Compiler/runtime supporting both detectors together |
| [lifetime](lifetime/clutch.toml) | Clang C++ lifetime diagnostics | Compiler accepting the requested newer lifetime-analysis warning group |

From this directory:

```sh
clutch .
clutch run
clutch address
clutch run --project address
clutch undefined
clutch run --project undefined
```

Replace the directory with another table entry to try that policy. Debug detector
variants select `strict`; release selects `normal`, retains optimization and enables
symbols. Configure and run with `--release` on both commands to select release:

```sh
clutch address --release
clutch run --project address --release
```

This release form is useful for clang-cl installations rejecting ASan with the
debug CRT. Unsupported detectors/flags/runtimes produce a configuration error;
Clutch does not silently disable them. Some sandbox/host restrictions can also
prevent an installed sanitizer runtime from starting. Use a supported host or
choose the root project when those checks are unavailable.

`normal` retains the detector's ordinary recovery policy. `strict` adds fatal
GNU-style sanitizer recovery policy, promotes enabled lifetime diagnostics and
makes selected `clutch lint` diagnostics errors. Microsoft-style ASan runtime
behavior is unchanged by strictness. Strictness alone enables no detector.
Lifetime diagnostics are compile-time checks; sanitizers need the relevant code
to execute. These policies do not implement a borrow checker or prove safety.

See [exact safety behavior](../../docs/manifest.md#safety-strictness-levels).
Contributors can run `clutch.tooling-safety` to exercise known runtime faults in
isolated test fixtures; see [testing](../../docs/testing.md). The [tooling example](../tooling/README.md)
also demonstrates formatter, analyzer and debugger commands.
