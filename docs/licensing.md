# Licensing

Clutch's own source and documentation use [BSD 3-Clause](../LICENSE).
Contribute only material you have the right to provide under those terms.
Contributors retain copyright; no copyright assignment is required.

Preserve existing copyright and license notices. For new project-owned source
files, include a copyright notice and `SPDX-License-Identifier: BSD-3-Clause`
separately from Doxygen documentation. Keep appropriate contributor attribution;
do not replace existing notices with an IDE-generated name. An optional
[CLion template](clion-copyright.md) is available.

Vendored code retains its upstream license and attribution:

| Library | License notice |
| --- | --- |
| CLI11 | [BSD 3-Clause](../third_party/CLI11.LICENSE) |
| toml++ | [MIT](../third_party/tomlplusplus.LICENSE) |
| JSON for Modern C++ | [MIT](../third_party/nlohmann.LICENSE) |

Do not apply project copyright automation to `third_party/`. See its
[maintenance notes](../third_party/README.md) when updating vendored code.

Preserve the complete license texts with source distributions and include them
in binary distributions as required by their terms. CMake installation and
release packaging place `LICENSE` and all three upstream notices under
`share/licenses/clutch/`. Copying a bare build executable does not package those
notices. The full license texts define the redistribution conditions.
