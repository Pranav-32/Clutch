#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Prove that only release metadata can bypass compiler-backed verification."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '.github/scripts'))
import ci_policy


@unittest.skipUnless(shutil.which('git'), 'Git is required for real diff fixtures')
class VerificationScopeTests(unittest.TestCase):
    """Use disposable Git history, including renames, deletions and new files."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch CI scope ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        (self.root / '.github').mkdir()
        (self.root / 'docs').mkdir()
        self.write_release('0.3.0')
        (self.root / 'source.cpp').write_text('int main() {}\n', encoding='utf-8')
        self.git('init', '-q')
        self.git('config', 'user.name', 'Clutch CI policy test')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'commit.gpgsign', 'false')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture baseline')
        self.base = self.git('rev-parse', 'HEAD')

    def git(self, *arguments):
        return subprocess.check_output(['git', '-C', str(self.root), *arguments], text=True).strip()

    def write_release(self, version):
        """Keep the fixture's release state internally consistent by default."""
        (self.root / 'VERSION').write_text(version + '\n', encoding='utf-8')
        (self.root / '.github/release-please-manifest.json').write_text(
            json.dumps({'.': version}), encoding='utf-8')
        (self.root / 'docs/CHANGELOG.md').write_text(
            f'# Changelog\n\n## [{version}](https://example.invalid/release)\n', encoding='utf-8')

    def commit(self):
        self.git('add', '-A')
        self.git('commit', '-qm', 'fixture change')

    def test_release_only_change_is_fast(self):
        self.write_release('0.4.0')
        self.commit()
        self.assertTrue(ci_policy.metadata_only(self.base, self.root))
        self.assertEqual(ci_policy.validate_metadata(self.root), '0.4.0')

    def test_source_and_workflow_edits_require_full_verification(self):
        self.write_release('0.4.0')
        for path in ('source.cpp', '.github/ci.yml', 'new-test.py'):
            with self.subTest(path=path):
                (self.root / path).write_text('changed\n', encoding='utf-8')
                self.commit()
                self.assertFalse(ci_policy.metadata_only(self.base, self.root))
                self.base = self.git('rev-parse', 'HEAD')

    def test_source_rename_cannot_hide_inside_metadata(self):
        (self.root / 'VERSION').unlink()
        self.commit()
        base = self.git('rev-parse', 'HEAD')
        self.git('mv', 'source.cpp', 'VERSION')
        self.commit()
        self.assertIn('R100', self.git('diff', '--name-status', '--find-renames=100%', base, 'HEAD'))
        self.assertFalse(ci_policy.metadata_only(base, self.root))

    def test_missing_or_invalid_history_defaults_to_full(self):
        for base in ('', '0' * 40, 'a' * 40, '--invalid', '../main', None):
            with self.subTest(base=base):
                self.assertFalse(ci_policy.metadata_only(base, self.root))
        self.assertFalse(ci_policy.metadata_only(self.base, self.root), 'empty diff skipped full CI')

    def test_deleted_source_requires_full_verification(self):
        (self.root / 'source.cpp').unlink()
        self.commit()
        self.assertFalse(ci_policy.metadata_only(self.base, self.root))

    def test_inconsistent_or_missing_metadata_fails(self):
        self.write_release('0.4.0')
        (self.root / '.github/release-please-manifest.json').write_text('{".": "0.3.0"}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'must match VERSION'):
            ci_policy.validate_metadata(self.root)
        self.write_release('0.4.0')
        (self.root / 'VERSION').unlink()
        with self.assertRaises(OSError):
            ci_policy.validate_metadata(self.root)
        self.write_release('0.4.01')
        with self.assertRaisesRegex(ValueError, 'canonical'):
            ci_policy.validate_metadata(self.root)

    def test_changelog_requires_exact_version(self):
        for heading in ('## [0.3.01]', '## 0.3.01', '## [0.3.0-rc.1]', '# 0.3.0'):
            with self.subTest(heading=heading):
                (self.root / 'docs/CHANGELOG.md').write_text(heading + '\n', encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'lacks the release entry'):
                    ci_policy.validate_metadata(self.root)
        self.write_release('0.4.0-rc.1')
        self.assertEqual(ci_policy.validate_metadata(self.root), '0.4.0-rc.1')


if __name__ == '__main__':
    unittest.main()
