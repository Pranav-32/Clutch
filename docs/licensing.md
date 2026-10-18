# Copyright and licensing

Clutch's project-owned code and documentation are licensed under the
[BSD 3-Clause license](../LICENSE), with the notice:

```text
Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
```

The SPDX license identifier is `BSD-3-Clause`. The root `LICENSE` contains the
license terms; this guide summarizes them and adds no license restrictions.
The [Open Source Initiative's BSD 3-Clause text](https://opensource.org/license/bsd-3-clause)
is the reference for the standard license.

## Use and redistribution

You may use, modify and redistribute Clutch, including commercially, provided
you meet the license conditions:

1. **Source distributions:** retain the copyright notice, all three conditions
   and the disclaimer. Keep the complete `LICENSE` with distributed source.
2. **Binary distributions:** reproduce the copyright notice, conditions and
   disclaimer in the accompanying documentation or other distribution materials.
   Include the complete `LICENSE` in release archives and installer packages.
3. **Endorsement:** obtain specific prior written permission before using the
   copyright holder's or contributors' names to endorse or promote a derived
   product. Keeping the required copyright attribution is separate from endorsement.

The software is provided without warranty, and the license limits liability as
specified in its disclaimer. BSD 3-Clause does not require you to publish your
modifications or the source of applications built using Clutch. Using Clutch as
a build tool does not by itself license your application under BSD 3-Clause;
code and libraries incorporated into that application have their own terms.

## Vendored libraries

The root license covers Clutch's own work. Vendored libraries retain their
upstream copyrights and licenses:

| Library | License | Notice to preserve |
| --- | --- | --- |
| CLI11 | BSD 3-Clause | [CLI11.LICENSE](../third_party/CLI11.LICENSE) |
| toml++ | MIT | [tomlplusplus.LICENSE](../third_party/tomlplusplus.LICENSE) |
| JSON for Modern C++ | MIT | [nlohmann.LICENSE](../third_party/nlohmann.LICENSE) |

Preserve upstream notices in source distributions. For Clutch binaries that
incorporate these headers, distribute their complete license notices too.
Do not replace upstream copyright holders with Clutch's copyright holder.
See [dependency maintenance](../third_party/README.md) when updating vendor code.

## Installation and release packaging

`cmake --install` installs the executable under `bin/` and the three license
files under `share/licenses/clutch/` in the selected prefix:

```text
share/licenses/clutch/
  LICENSE
  CLI11.LICENSE
  tomlplusplus.LICENSE
  nlohmann.LICENSE
```

When creating a release archive or installer, include those notices with the
executable. When copying a binary directly out of a build directory, copy the
root `LICENSE` and all `third_party/*.LICENSE` files into its distribution
materials as well; compiling alone does not package them.

## Per-file notices and IDE automation

Project-owned source files can use a short copyright comment containing
`SPDX-License-Identifier: BSD-3-Clause`. Keep it separate from Doxygen file
documentation. The identifier supplements the full `LICENSE` and does not
replace the source/binary redistribution notices described above.

Use the [CLion setup guide](clion-copyright.md) and
[copyable Velocity template](clion-copyright-template.txt) to insert notices and
update year ranges on save or on IDE commits. Scope automation to Clutch's own
sources; preserve upstream and contributors' individual notices. The collective
credit does not require contributors to assign their copyright to the maintainer.

## Contributions

Contributions to Clutch's own code and documentation must be available under
BSD 3-Clause. Submit only material you have the right to contribute under those
terms. Preserve existing copyright and license notices, including those on
third-party code. Contributors retain copyright in their contributions; the
project does not require copyright assignment.
