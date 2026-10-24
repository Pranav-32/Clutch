# Named commands and lifecycle hooks

Audience: Users trying scripts around native configure/build workflows.

Install a C++ compiler, a supported generator executor, Clutch and Python 3.
This example declares `python3`; if your interpreter is named `python` or lives
elsewhere, change each command's first `argv` entry accordingly. Python is used
by this project's scripts, not by Clutch itself.

From this directory:

```sh
clutch exec hello -- "argument with spaces"
clutch .
clutch run
clutch build
```

The manual command works before configuration. Configure runs `prepare` then
`configured`; the latter checks that the compilation database exists. Each CLI
build runs `resources` before compilation and `finished` after successful linking
and runtime staging, including when no object needs rebuilding. `run` then starts
the executable. The resource is written under the selected profile directory.

Try `clutch watch --run --path scripts` to repeat the workflow after source or
script edits. The generated resource stays beneath the excluded build directory,
so it does not trigger another cycle. The application exits normally after each
run; watch waits for the next edit.

Commands use literal argument arrays and run from the manifest directory by
default. Try `clutch exec hello --release` to see the independent release context.
For Node or another tool, replace `argv` with that interpreter/executable and its
arguments. See [command fields and lifecycle boundaries](../../docs/manifest.md#commands-and-lifecycle-hooks),
including failure handling and the difference from generated-output rules.

## Other command tools

Keep this collection together: these variants share the parent's `main.cpp`.
Manual commands need only their declared tool, not a compiler/configuration:

| Variant | From this directory | Required tool |
| --- | --- | --- |
| [Native executable](native/clutch.toml) | `clutch exec git-version --project native` | Git |
| [Node](node/clutch.toml) | `clutch exec hello --project node -- "two words"` | Node; no npm packages |
| [POSIX shell](shell-posix/clutch.toml) | `clutch exec hello --project shell-posix` | `sh` |
| [Windows shell](shell-windows/clutch.toml) | `clutch exec hello --project shell-windows` | Windows `cmd.exe` |

The Node variant also runs an after-build command: configure `node/`, then run
`clutch build --project node`. Shell syntax is evaluated only in the two explicit
shell variants. The Windows variant uses a `.cmd` script to avoid nested command
string quoting; keep more involved shell programs in scripts too. Ordinary argv
values remain literal. For a PowerShell script,
use an argv array such as `["pwsh", "-NoProfile", "-File", "scripts/check.ps1"]`
with that interpreter installed. The Python root example exercises all four
lifecycle phases and a manual command; none of these tools is embedded in Clutch.
