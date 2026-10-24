# Developer tools and profile safety

Audience: Users trying optional formatting, analysis and debugging commands.

From this directory, with Clutch and the selected tools on PATH:

```sh
clutch fmt --check
clutch .
clutch lint
clutch run
clutch debug --target tooling_example
```

The debug profile leaves optional detectors disabled, so trying developer tools
does not require a sanitizer runtime. To request instrumentation, add `"address"`
to its sanitizer array or try the [safety examples](../safety/README.md).
Configuration checks compiler/linker support. On supported Windows Clang/MSVC
installations, Clutch stages the compiler's AddressSanitizer DLLs beside the
executable. Install the matching runtime before requesting it. This safe program
should finish normally.

The release profile keeps optimization while enabling symbols:

```sh
clutch . --release
clutch debug --release --target tooling_example
```

Install clang-format for `fmt`, clang-tidy for `lint`, and LLDB/GDB/CDB for `debug`.
Each command accepts `--tool <path>`. See the [tooling commands](../../docs/usage.md#format-analyze-and-debug)
and [profile fields](../../docs/manifest.md#safety) for exact scope and limitations.
