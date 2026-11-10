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

Tools can also come from a supported IDE installation. For a custom CLion
location, set its root in the current terminal before trying discovery:

```powershell
$env:CLION_HOME = "C:\path\to\CLion"
clutch debug --target tooling_example
```

Automatic debugger selection checks startup and skips unusable candidates.
`--tool` or `CLUTCH_DEBUGGER` pins an exact choice; `CLUTCH_FORMATTER` and
`CLUTCH_ANALYZER` select the other optional tools. See
[tool discovery](../../docs/usage.md#configure-then-build) for platform locations,
precedence and SDK requirements.
