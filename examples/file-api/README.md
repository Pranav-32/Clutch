# File-API client example

Clutch serves this API directly in C++. It requires no Python runtime, daemon or
server. Python is used only for this optional demonstration client and Clutch's
development tests; IDE extensions can read these JSON files in their own language.

## Using it from an IDE

Existing IDEs do not automatically recognize Clutch's query/reply protocol. A
Clutch extension needs to call `clutch locate --project <source>`, create its query
in the reported directory, run configuration, then follow the newest successful
index into target and toolchain objects. The extension maps those objects into its
IDE's project model and language service. CMake extensions cannot consume it
unchanged.

Until a Clutch extension is available, point your IDE at the generated compilation
database, or open the generated Visual Studio/Xcode project. See the
[usage guide](../../docs/usage.md#build-directories-and-ides) for CLion, clangd
and VS Code settings.

## Running the optional client

This directory contains a client for Clutch projects, rather than a project
manifest. Build Clutch first, then use Python 3.9+ to run the client:

```sh
python examples/file-api/client.py --clutch /absolute/path/to/clutch --project examples/mixed
```

On Windows, use an absolute path to `clutch.exe` from a compiler developer shell.
Optional `--cc`, `--cxx` and `--build-tool` arguments select installed tools.

The client discovers capabilities and profile paths, writes its own query,
configures the project, and prints the code model and target objects as JSON.
It checks the CLI exit status before reading replies and executes no action
received in a reply.

Use `--release` for that profile, or `--read-only` to inspect an existing snapshot.
The reader rejects a failed configuration, a changed manifest or an active writer
lock. It checks every recorded root/member manifest and `clutch.lock` input.
Source additions/removals and toolchain/environment changes also require
reconfiguration; the reader is not a filesystem watcher.

`query.json` is a copyable query for a separate IDE client. Place it under the
selected build directory's `.clutch/api/v1/query/client-<name>/` before configuring.
See the [protocol guide](../../docs/file-api.md) for ownership, version negotiation
and reference-resolution rules.
