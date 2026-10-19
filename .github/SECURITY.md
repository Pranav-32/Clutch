# Security policy

## Supported versions

Security fixes target the [latest stable release](https://github.com/Pranav-32/Clutch/releases/latest).
Upgrade to that release before checking whether a reported issue is resolved.
Older releases do not receive separate security backports. Development builds
from `main` may contain fixes that have not been released yet.

## Report a vulnerability

Use [GitHub's private vulnerability reporting form](https://github.com/Pranav-32/Clutch/security/advisories/new).
Keep vulnerability details and reproductions out of public issues and pull
requests until a fix and disclosure have been coordinated.

Include the following information when available:

- Clutch version or commit, operating system, compiler and generator.
- A minimal manifest and reproduction, including the commands you ran.
- Expected behavior, observed behavior and the potential security impact.
- Relevant logs with credentials, tokens and personal information removed.

Maintainers will review the report and coordinate reproduction, remediation and
disclosure with you. Ordinary build failures and feature requests belong in
[public issues](https://github.com/Pranav-32/Clutch/issues).

## Build inputs and dependency coverage

Treat manifests, source trees and configured executables as trusted build
inputs. Clutch invokes native compilers and build tools, and its `run` and `test`
commands execute project programs. Project-local build directories provide
output isolation, not a security sandbox.

Report unintended writes outside generated outputs, argument-handling flaws or
other behavior that crosses these boundaries through the private channel above.

GitHub code scanning and Dependabot supplement maintainer review. Vendored C++
headers in `third_party/` require upstream advisory and version review; enabling
Dependabot does not automatically register them as managed dependencies.
