# Build, run and test while editing

Requires Clutch, a C++17 compiler and a compatible executor. From this directory:

```sh
clutch watch
clutch watch --run --target hello --path assets --ignore assets/generated
clutch watch --test --target message-test
clutch watch --run --target hello --release --jobs 2 -- assets/message.txt "two words"
```

Run one watcher at a time; Ctrl+C stops it. Watch configures a fresh project and
builds immediately. To choose compilers/generator explicitly, configure first
with `clutch . --cc ... --cxx ... --generator ...`.

| Try this edit | Expected behavior |
| --- | --- |
| Change `message/message.cpp` or `.hpp` | Rebuild the library and affected consumers. |
| Change `assets/message.txt` with `--path assets` | Run again and print the new resource text; compilation may already be current. |
| Change `assets/generated/README.txt` with the ignore above | No cycle. |
| Add a target/source selection in `clutch.toml` | Reconfigure, then build the selected targets. |
| Introduce a C++ syntax error, then fix it | Report the failed cycle, remain watching and recover on the next edit. |
| Save several times quickly | Coalesce the burst into one debounced cycle. |

Try `--interval 500 --debounce 300` to adjust polling/quiet periods. Build outputs
and dependency caches are excluded automatically. Extra paths may also be absolute.
The application exits after printing; run mode does not restart a long-running
server. The [workspace example](../workspace/README.md) works with watch too; use
qualified targets such as `hello::hello` when selecting a member application.
See [watch options and boundaries](../../docs/usage.md#watch-for-changes).
