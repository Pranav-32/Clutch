# Versions and CI

Audience: Users and contributors checking version identity and CI expectations. Release
operators should also read the [maintainer guide](maintaining.md).

[`VERSION`](../VERSION) records Clutch's release version. Release history is in
the [changelog](CHANGELOG.md); published tags and assets are listed on
[GitHub Releases](https://github.com/Pranav-32/Clutch/releases).

## Version identity

Clutch uses Semantic Versioning. CMake generates `clutch --version` from
`VERSION` and Git metadata when available:

| Source state | Example output, with `VERSION` set to `0.3.0` |
| --- | --- |
| Clean commit with matching `v0.3.0` tag | `clutch 0.3.0` |
| Untagged development commit | `clutch 0.3.0-dev+g0123456789ab` |
| Local changes | `clutch 0.3.0-dev+g0123456789ab.dirty` |
| Shallow development checkout | `clutch 0.3.0-dev+g0123456789ab.shallow` |
| Source archive without Git metadata | `clutch 0.3.0+source` |

Dirty and shallow markers can appear together. The identity refreshes during
the build; the installed executable does not need Git to report its version.
Example project versions, lock schemas and file-API versions are independent.

## Contribution titles and version proposals

Use Conventional Commit titles for pull requests. The Version proposal
workflow uses the merged history to maintain a reviewable PR updating `VERSION`,
`docs/CHANGELOG.md` and `.github/release-please-manifest.json`.

| Title | Version effect under the checked-in policy |
| --- | --- |
| `fix: ...` | Patch |
| `feat: ...` | Minor |
| `feat!: ...` or a `BREAKING CHANGE:` footer | Major |
| `docs: ...`, `chore: ...` | No release by itself |

Contributors do not need to edit version files or create tags for ordinary
changes. Version proposals and publication are reviewed separately.

## Continuous verification

CI runs on pull requests, pushes to `main` and manual requests. Superseded
runs for the same PR are cancelled; a feature-branch push does not duplicate
the PR matrix.

| Native target | Required generator coverage |
| --- | --- |
| Linux GCC and Clang, x86_64 | Ninja and GNU Make |
| Windows MSVC, x86_64 | Ninja, NMake and Visual Studio |
| macOS Apple Clang, ARM64 and x86_64 | Ninja, GNU Make and Xcode |

Source changes build Clutch, run registered tests and shipped examples in both
profiles, verify installation and package binaries with license notices. A
separate job checks the development image. JOM and Windows GNU Make are checked
when available. See the workflow files for runner versions and tool settings.

Implementation details, metadata-only selection and release operations are
documented in the [maintainer guide](maintaining.md). Ordinary contributions
do not need to change workflows or version files.

Run the same pipeline locally with the target's tools on `PATH`, for example:

```sh
CC=gcc CXX=g++ python .github/scripts/ci.py --target linux-x86_64-gcc
```

Python 3.12+ is required for this CI driver. `CLUTCH_BUILD_JOBS` and
`CLUTCH_TEST_JOBS` default to `2`; `CLUTCH_COMPILER_LAUNCHER` optionally selects
an installed compiler cache. These settings concern Clutch development, not
user-project manifests. See [testing](testing.md) for native test commands.

## Release artifacts

The Release workflow validates that a pushed `v<version>` tag matches `VERSION`,
verifies all five native targets, checks their archive checksums and publishes
only after the complete asset set is uploaded. Failed uploads leave a resumable
draft. Published tags and releases are not overwritten.

Archives contain the executable and Clutch's required license notices, with
SHA-256 sidecars and `SHA256SUMS`. Prerelease versions become GitHub prereleases.
Linux binaries require compatible Ubuntu 22.04 system/C++ runtimes; macOS
binaries target macOS 14 or newer. Windows packages use the static MSVC runtime.
Archives are currently unsigned; macOS binaries are not notarized.
