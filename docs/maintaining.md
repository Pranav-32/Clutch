# Maintainer guide

Audience: maintainers and contributors changing repository automation. Ordinary
contributions do not require version-file, workflow or release-setting changes.

## Triage and review

Keep bug reports focused on reproducible behavior. Ask for missing platform,
compiler, generator and manifest information without requiring reporters to know
the implementation. Route vulnerabilities through the [security policy](../.github/SECURITY.md).
Apply `good first issue` to bounded work with enough context for a newcomer, and
`help wanted` when outside help is useful. These labels describe scope and readiness,
not a guarantee of immediate review.

Review behavior, compatibility, ownership and failure paths as well as code
style. Explain requested changes and distinguish required fixes from suggestions.
Check affected documentation and examples. Required CI checks must pass before
merge; inspect platform failures rather than treating unrelated local success as
equivalent coverage. Follow the [code of conduct](../.github/CODE_OF_CONDUCT.md)
when moderating discussions. No response-time commitment is implied.

## Automation map

| File | Responsibility |
| --- | --- |
| `.github/workflows/ci.yml` | PR/main verification, cancellation and development-container checks |
| `.github/workflows/build.yml` | Reusable five-target native matrix, examples, installation and packaging |
| `.github/workflows/version.yml` | Reviewable Conventional Commit version proposals; publication is disabled here |
| `.github/workflows/release.yml` | Validate a version tag, verify its exact commit and publish complete artifacts |
| `.github/scripts/ci_policy.py` | Classify changed content and verify release metadata |
| `.github/scripts/ci.py` | Host-native build/test/install/package driver |
| `.github/scripts/release.py` | Validate tags, verify checksums and publish a complete draft |
| `cmake/ClutchVersion.cmake` / `cmake/GenerateVersion.cmake` | Build-time version identity from VERSION and Git metadata |

Python is required by development automation, not the installed executable.
After changing these scripts, run the tooling tests and relevant native checks:

```sh
python -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

Validate workflow syntax with actionlint. Keep referenced actions pinned and
review Dependabot updates.

The native matrix covers Linux GCC/Clang, Windows MSVC, and both supported macOS
architectures. It exercises each host's available native generators. Source changes
also run examples in both profiles, verify installation and package notices.
The development-container job checks the shared Linux environment separately.

Only changes limited to `VERSION`, `.github/release-please-manifest.json` and
`docs/CHANGELOG.md` take the metadata-only path. Classification uses the Git diff,
not titles, labels or branch names. Unknown history and manual runs receive full
coverage. Preserve the five required check names in both paths. Compiler caches
may be reused; saved test results and release packages are not cached.

## Version proposals and publication

1. Merge reviewed changes with [Conventional Commit titles](releases.md#contribution-titles-and-version-proposals).
   The version workflow proposes matching `VERSION`, changelog and release-please
   manifest updates. Ordinary contributors do not edit those files to request a bump.
2. Review the proposed version, notes and checks, then merge the proposal through
   the normal PR process. This workflow does not itself publish a tag or release.
3. At the merged release commit, create and push the annotated `v<version>` tag
   matching `VERSION`. Confirm the worktree and commit before tagging. Do not
   move published tags to repair a release.
4. The tag-driven release workflow validates identity and runs full verification
   from that exact commit. It does not promote binaries built for a PR.
5. Verify the published release contains all five archives, their SHA-256 sidecars,
   the combined `SHA256SUMS`, and license notices. A failed upload leaves a draft
   that the workflow can resume. Published releases are not overwritten.

For a prerelease, maintainers can put a `Release-As` footer with the desired
canonical prerelease version in the squash commit body. Release Please then
proposes that exact version through the usual review process; see its
[version override documentation](https://github.com/googleapis/release-please#how-do-i-change-the-version-number).
The publication script marks versions containing a prerelease suffix as GitHub
prereleases. The same full verification and asset checks apply.

Repository settings must allow the workflows' declared permissions and satisfy
the configured branch rules. `RELEASE_PR_TOKEN`, when configured, allows the
version bot's PR to trigger ordinary CI events. With only `GITHUB_TOKEN`, GitHub's
event suppression means proposal checks need a separate workflow dispatch.
Keep credentials in repository secrets and out of source files or logs.

## Security maintenance

GitHub-managed CodeQL scans C/C++, Python and GitHub Actions. Review findings in
their input/trust context and record specific triage reasons; do not disable
analysis to clear alerts. Dependabot manages pinned Actions updates. Review
vendored C++ headers against upstream versions/advisories separately. See the
[dependency notes](../third_party/README.md) and [security policy](../.github/SECURITY.md).
