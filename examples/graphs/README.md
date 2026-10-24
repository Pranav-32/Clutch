# Inspect and render a build graph

With Clutch, a C++17 compiler and a compatible executor, configure the static
library example from the repository root:

```sh
clutch examples/targets
clutch targets --project examples/targets
```

Clutch writes `examples/targets/Clutch-build-debug/build-graph.dot` without needing
Graphviz. Inspect the text, or install Graphviz and render it:

```sh
dot -Tsvg examples/targets/Clutch-build-debug/build-graph.dot -o targets.svg
```

The graph shows the static library target's compile/link prerequisites and its
application/test consumers. Repeat with `examples/workspace`, `examples/pch`
or a dependency example to inspect member, PCH and package edges. Clutch validates
cycles and duplicate producers independently of rendering. Compiler-discovered
header dependencies are not all drawn in this logical graph. See [graph usage](../../docs/usage.md#build-directories-and-ides).
