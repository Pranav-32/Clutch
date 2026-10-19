# CLion copyright template

Audience: Contributors who want optional CLion copyright automation.

The optional [template](clion-copyright-template.txt) includes the
`BSD-3-Clause` SPDX identifier, preserves the original year and credits Pranav
Sanjeevi Kumari and contributors. Preserve individual contributors' existing
notices and all upstream attribution; see [licensing](licensing.md).

1. Open **Settings > Editor > Copyright > Copyright Profiles** and add a
   **Clutch BSD-3-Clause** profile.
2. Paste the template as plain text without comment delimiters, then validate it.
3. Set the detection regex to `Copyright [(]c[)].*Pranav Sanjeevi Kumari` and
   allow replacement only when the existing notice matches
   `Pranav Sanjeevi Kumari`.
4. Under copyright formatting for C/C++, use a block comment before other
   comments, followed by a blank line. Keep Doxygen documentation separate.
5. Assign the profile to a scope containing only maintained source files under
   `src/`, `tests/unit/` and the source examples. Exclude `third_party/`, generated
   builds, test fixtures and license texts. Preview the scope before applying it.

Leave the default project copyright unassigned to avoid editing unrelated files.
Local `.idea/` settings remain untracked. Enable **Update copyright notice** in
**Actions on Save**, or **Update copyright** in IDE commit checks, if desired.
Terminal commits do not run IDE checks. Review the diff after bulk updates.

For new files, the notice has this form:

```cpp
/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */
```
