# Vendored dependencies

These header-only libraries are pinned and included with the source tree, so the
CMake development build does not download dependencies or need runtime libraries.
Their upstream code is kept separate from Clutch's co-located modules under `src/`.

| Dependency | Version | Header | License notice |
| --- | --- | --- | --- |
| CLI11 | 2.4.2 | [CLI11.hpp](CLI11.hpp) | [CLI11.LICENSE](CLI11.LICENSE) |
| toml++ | 3.4.0 | [toml.hpp](toml.hpp) | [tomlplusplus.LICENSE](tomlplusplus.LICENSE) |
| JSON for Modern C++ | 3.12.0 | [nlohmann/json.hpp](nlohmann/json.hpp) | [nlohmann.LICENSE](nlohmann.LICENSE) |

These libraries retain their upstream copyrights and licenses. Clutch's root
[license](../LICENSE) does not replace those notices. Distribute all upstream notices
with Clutch binaries incorporating these headers; `cmake --install` includes
them under `share/licenses/clutch/`. See the [licensing guide](../docs/licensing.md).

CLI11 implements argument parsing; toml++ reads the project manifest and saved
tool selections; nlohmann JSON parses file-API queries and serializes structured
replies. CMake exposes this directory as a system include path to keep
upstream diagnostics separate from owned code. Clutch uses `<CLI11.hpp>` and
`<toml.hpp>` and `<nlohmann/json.hpp>`; its private module includes are anchored at `src/`.

The JSON header is the upstream v3.12.0 single-header distribution. Its SHA-256
is `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`,
verified against the [upstream release](https://github.com/nlohmann/json/releases/tag/v3.12.0).

When updating a library, replace its single header and matching license notice
together, verify the embedded version, and run the compiler-backed integration
checks. Do not apply project refactoring or lint fixes to upstream code.

The local `.clang-format` disables formatting for these dependencies, and the
root formatting exclusions also skip this directory. The root `.clang-tidy`
restricts additional header diagnostics to project-owned `src/` headers.
