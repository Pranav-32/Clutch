# CLion copyright automation

This profile credits Pranav Sanjeevi Kumari and contributors for Clutch's
project-owned work. Contributors retain copyright in their own contributions;
preserve individual authors' notices. The root
[LICENSE](../LICENSE) and [licensing guide](licensing.md) remain authoritative.

## Create the profile

1. Open **Settings > Editor > Copyright > Copyright Profiles**.
2. Add a profile named **Clutch BSD-3-Clause**. Keep this personal profile local.
3. Paste [the template](clion-copyright-template.txt) as plain text, without
   comment delimiters. Click **Validate**.
4. Set the detection and replacement fields:

| Field | Value |
| --- | --- |
| Regex to detect copyright in comments | `Copyright [(]c[)].*Pranav Sanjeevi Kumari` |
| Allow replacing copyright if old copyright matches | `Pranav Sanjeevi Kumari` |

These expressions recognize both the original name-only notice and the notice
including contributors. Paste the updated template into your existing CLion
profile and run **Update Copyright** to update existing headers; the IDE stores
its own copy of the template.

Under **Editor > Copyright > Formatting > C/C++**, use a normal block comment
before other comments, with a blank line afterward. Preserve the separate
Doxygen `@file` block. See [CLion's copyright documentation](https://www.jetbrains.com/help/clion/copyright.html).

## Restrict its scope

Create a scope named **Clutch-owned sources** under
**Settings > Appearance & Behavior > Scopes**. Enable **Share through VCS** for
the scope so it is available in copyright settings. Use this pattern:

```text
file:src//*||file:tests/*.cpp||file:tests/*.hpp||file:examples/single-file/*.cpp||file:examples/c-project/*.c||file:examples/multi-file/src//*||file:examples/multi-file/tests//*||file:examples/mixed/src//*
```

These positive paths cover maintained sources and exclude `third_party/`,
generated build directories, test fixtures, license files and this template.
Extend the scope when adding maintained source directories. Inspect the selected
files in the scope preview before applying it. See the
[scope syntax reference](https://www.jetbrains.com/help/clion/scope-language-syntax-reference.html).

Under **Editor > Copyright**, assign this scope to **Clutch BSD-3-Clause**.
Leave **Default project copyright** unassigned. `.idea/` remains ignored by this
repository; the scope's sharing flag does not itself commit its settings.

## Automate updates

- Enable **Tools > Actions on Save > Update copyright notice** for saved edits.
  CLion supports both explicit saves and autosaves; see
  [actions on save](https://www.jetbrains.com/help/clion/saving-and-reverting-changes.html).
- Enable **Update copyright** in the Commit tool window's **Commit Checks** for
  CLion commits. A terminal `git commit` does not run IDE commit checks. See
  [commit checks](https://www.jetbrains.com/help/clion/commit-and-push-changes.html).
- For existing files, right-click a maintained source folder and choose
  **Update Copyright**, using the assigned scope. Preview the diff first.

The template includes `SPDX-License-Identifier: BSD-3-Clause`. Its year matcher
retains the first year from an existing notice and suppresses a duplicate
current year using the fourth `match` argument. In 2026, a new notice reads:

```cpp
/*
 * Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE in the project root for the full license text.
 */
```

A later update can produce `2026-2027`. The matcher uses the IDE's documented
Velocity context and [year-matching implementation](https://github.com/JetBrains/intellij-community/blob/cb3d161687f1306eda1106d2ea98a6407cc17bb6/plugins/copyright/src/com/maddyhome/idea/copyright/pattern/CommentInfo.java).
The short notice supplements the full license and upstream distribution notices.
