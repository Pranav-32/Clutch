# Bundle, extract and rebuild sources

Use the self-contained [watch project](../watch/README.md), which has a library,
application, tests and a runtime resource. Run from the repository root:

```sh
clutch bundle --project examples/watch --output watch-sources.zip --exclude assets/generated
```

`watch-sources.zip` must be a new output path. Extract it into a fresh directory
with your archive tool, then run from that extracted root:

```sh
clutch .
clutch run --target hello
clutch test
clutch . --release
clutch run --release --target hello
```

The application reads the bundled `assets/message.txt`. Profile rules, IDE
metadata and the lock are regenerated for the recipient. The compiler and build
executor must be installed on that machine; the archive is a source distribution.

For a resolved source dependency example, bundle `examples/dependencies` the same
way. Its relative local/installed example prefixes stay together in the snapshot.
Real installed SDKs and their tools still need installation on the recipient.
The [dependency examples](../dependencies/README.md) show offline/locked rebuilds,
and [source bundles](../../docs/usage.md#source-bundles) explain exclusions,
dependency relocation, runtime limitations and files included regardless of
`.gitignore`.
