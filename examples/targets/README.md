# Local library targets

Configure in this directory, then build, run and test:

```sh
clutch .
clutch targets
clutch build --target hello
clutch run --target hello
clutch test --target greeting_test
```

`greeting` is a static library shared by the application and test. Its public
header directory and definition propagate through `links`; its sources compile
once per profile. `clutch build` builds all ordinary default targets and leaves
tests opt-in. See the [target reference](../../docs/manifest.md#target) for shared
libraries, public/private requirements and multiple applications.

Application shorthand can coexist with these library declarations. Use
`project.entry` or `build.sources` for the application named by `project.name`
and `build.links` for its libraries. An explicit executable with that name can
extend the application's settings while inheriting those source defaults.
