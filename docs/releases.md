# Versions, CI and releases

User-project `clutch.lock` files have their own resolution schema, independent
of Clutch's root `VERSION`, Git release tags, file-API versions and private build
cache versions. Release Please does not resolve workspace dependencies or update
user locks. After deliberately changing a member's project version, configure
its workspace normally and review/commit the resulting lock change. See
[lock files](lock-files.md) for the current local-only resolution scope.

Clutch uses Semantic Versioning to describe shipped changes. During
early development use `0.x.y`, and move to `1.0.0` when the supported interface
and compatibility commitments are ready. Do not keep unrelated releases at 0.1.0.

## Release identity

[`VERSION`](../VERSION) holds the release version, including an optional
prerelease such as `0.2.0-rc.1`. CMake reads its numeric core for project metadata
and generates the CLI identity from Git. Example projects have independent
versions; changing Clutch's version does not change their manifests.

| Source state | Example `clutch --version` |
| --- | --- |
| Clean commit tagged `v0.1.0`, with matching VERSION | `clutch 0.1.0` |
| Untagged development commit | `clutch 0.1.0-dev+g0123456789ab` |
| Tracked edits or unignored new files | `clutch 0.1.0-dev+g0123456789ab.dirty` |
| Shallow development checkout | `clutch 0.1.0-dev+g0123456789ab.shallow` |
| Source archive, unavailable Git or unreadable Git metadata | `clutch 0.1.0+source` |

Dirty and shallow markers can appear together. Prerelease development builds
append `.dev`, for example `0.2.0-rc.1.dev+g0123456789ab`. Hash abbreviations
are at least 12 hexadecimal digits. These identify source provenance, not an
automatically inferred next release number. Git is optional at build time and
is never invoked by the installed executable to determine its own version.

The version generator runs at configuration and before each executable build.
It writes the generated header only when its contents change. Commits, tags and
local edits therefore refresh the identity without an unnecessary CLI recompile
on unchanged builds. Linked Git worktrees and detached checkouts are supported.
CI fetches full history and tags. Source archives remain buildable offline.

## Automatic version proposals

The **Version proposal** workflow runs on pushes to `main` and can be started
manually. [Release Please](https://github.com/googleapis/release-please-action)
maintains a pull request updating VERSION, this directory's
[changelog](CHANGELOG.md), and its version-state manifest. It does not merge its
own PR or publish a release. Review and merge the proposal, then tag that commit.

Use Conventional Commit messages, especially squash-merge titles:

| Message | Proposed change |
| --- | --- |
| `fix: preserve compiler exit codes` | Patch: `0.1.0` → `0.1.1` |
| `feat: support library targets` | Minor: `0.1.0` → `0.2.0` |
| `feat!: change manifest syntax` or a `BREAKING CHANGE:` footer | Major: `0.1.0` → `1.0.0` |
| `docs: clarify compiler setup` or `chore: update tooling` | No release by itself |

The explicit pre-1.0 settings retain this mapping for 0.x releases. The current
release baseline is 0.2.0. A `fix:` change proposes 0.2.1; a `feat:` change
proposes 0.3.0. When both are included, the feature's minor bump takes precedence.
Release PRs maintain VERSION and the automation state automatically. If changing
versions manually, also update `.github/release-please-manifest.json` to the same
version so subsequent proposals start from the correct baseline.

Enable **Settings → Actions → General → Allow GitHub Actions to create and
approve pull requests**. The workflow uses the built-in token by default.
GitHub suppresses ordinary workflow events from PRs created with that token;
this behavior is documented by [Release Please](https://github.com/googleapis/release-please-action#other-actions-on-release-please-prs).
For automatic PR checks, set the repository secret `RELEASE_PR_TOKEN` to a
dedicated bot's fine-grained token with repository Contents and Pull requests
read/write permissions, plus Issues read/write for release labels. Restrict it
to this repository. Otherwise run **CI → Run workflow** against the proposal's
branch and inspect the results before merging. A manual run is not a substitute
for a required PR status check; repositories requiring automatic PR checks
should configure the bot token. No personal token is needed for tag publishing.

## Continuous verification

CI runs on branch pushes, pull requests and manual requests. Native jobs build
Clutch in Release mode, require the relevant compiler-backed CTest suites, run
all shipped examples in debug and release profiles, test installation, and
create downloadable workflow artifacts containing the binary and licenses.
Each example with `[[test]]` entries also runs its tests.

| Runner | Compiler / architecture | Native integration |
| --- | --- | --- |
| Ubuntu 22.04 | GCC / x86_64 | Ninja and GNU Make |
| Ubuntu 22.04 | Clang / x86_64 | Ninja and GNU Make |
| Windows Server 2022 | MSVC / x86_64 | Ninja, NMake and Visual Studio/MSBuild |
| macOS 14 | Apple Clang / ARM64 | Ninja, GNU Make and Xcode |
| macOS 15 Intel | Apple Clang / x86_64 | Ninja, GNU Make and Xcode |

JOM and Windows GNU Make are exercised if present, with explicit skips otherwise.
Linux/macOS jobs install Graphviz to validate actual DOT rendering. A separate
Linux job builds the development image and runs its mounted-workspace checks.
Workflow action dependencies are pinned to reviewed commit hashes. Update their
hashes deliberately when upgrading; comments record the corresponding releases.

Linux release archives target the Ubuntu 22.04 environment and require compatible
system/C++ runtimes; they are not universally static binaries. macOS artifacts
target macOS 14 or newer. Windows uses the static MSVC runtime. Archives are
currently unsigned and macOS binaries are not notarized. Native Apple execution
must be validated on GitHub's runners; a Linux container cannot establish it.

To run the same native pipeline locally with Python 3.12+ and the platform's
required tools on PATH:

```sh
python .github/scripts/ci.py --target linux-x86_64-gcc
```

Set `CC=gcc` and `CXX=g++` for that target, or select its Clang counterpart and
matching compiler variables. Windows requires an MSVC developer shell; macOS
requires Xcode, Ninja, GNU Make (`gmake`) and Graphviz. Build output defaults to
`build-ci/`, packages to `dist/`. `--build-dir` and `--output` here are CI script
options, unrelated to Clutch project profiles.

Tooling regression tests create temporary Git repositories and install trees:

```sh
python -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

Python is only a development dependency. Git, CMake and Python tests never become
runtime dependencies of the shipped Clutch binary.

## Publish a version

Push this configuration to a GitHub repository and enable Actions. No remote is
hard-coded into these workflows: they use the current repository and its built-in
token. Configure branch protection to require the CI checks appropriate to your
supported platforms before merging release changes.

After VERSION and the changelog are reviewed, commit all changes and use a clean
checkout of the exact release commit. For the initial baseline:

```sh
git tag -a v0.1.0 -m "Clutch 0.1.0"
git push origin main
git push origin v0.1.0
```

Use the actual version for subsequent releases. The **Release** workflow:

1. Verifies the tag equals `v` plus VERSION and resolves to the checked-out commit.
2. Builds and tests all five native targets at that exact commit.
3. Downloads their packages and checks the complete platform set and SHA-256 files.
4. Creates or resumes a draft, uploads all archives, license notices and checksums,
   and publishes only after the expected assets are present.

Archives contain `bin/clutch` (or `clutch.exe`) and the four required notices
under `share/licenses/clutch/`. Each archive has a `.sha256` sidecar; the release
also provides `SHA256SUMS`. Versions with a prerelease suffix become GitHub
prereleases. Ordinary commits produce CI artifacts without publishing releases.

If a build fails, no publishing job runs. An upload failure leaves a draft that
can be resumed by rerunning the workflow. Published releases are not overwritten;
use a new version for corrections. The manual Release action accepts an existing
tag, which is useful for retries. Do not move published tags.

Local checks validate workflow syntax, versioning and packaging; an actual
GitHub run is still necessary to establish runner availability, repository
permissions and native macOS behavior. No release is created merely by adding
these files to a local checkout.
