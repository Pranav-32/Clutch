# Shared library, public API and runtime staging

Requires Clutch, a C++17 compiler and a compatible executor. From this directory:

```sh
clutch .
clutch targets
clutch run --target hello
clutch test --target answer-test
clutch build --clean --target hello
```

`hello` prints `Shared answer: 42`. Both consumers link the same `answer` shared
target and inherit its public include directory. `ANSWER_BUILD` remains private
to the library, so Windows consumers see `dllimport` while its implementation
sees `dllexport`. Unix-like hosts use the same API without those attributes.

Windows builds produce a DLL and an import library; Clutch stages required runtime
DLLs beside consumers. POSIX builds use the generator's shared-library runtime
lookup rules. `--clean` exercises rebuilding the selected executable and its
library dependency. Configure/run/test with `--release` to use the other profile.
See the [static-library example](../targets/README.md) and [target reference](../../docs/manifest.md#target).
