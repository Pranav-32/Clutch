#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Regression checks for release identity, archive contents and publishing gates."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.github/scripts'))
import release


class ReleaseTests(unittest.TestCase):
    """Use disposable install trees; never contact GitHub or publish a release."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version = '1.2.3'

    def assets(self):
        """Construct the complete platform set without executing cross-host binaries."""
        for target in release.TARGETS:
            archive = self.root / release.archive_name(self.version, target)
            archive.write_bytes(target.encode())
            archive.with_name(archive.name + '.sha256').write_text(
                f'{release.checksum(archive)}  {archive.name}\n', encoding='utf-8'
            )

    def test_canonical_versions(self):
        for value in ('0.1.0', '12.34.56', '1.0.0-rc.1', '1.0.0-123abc', '1.0.0-0'):
            with self.subTest(value=value):
                (self.root / 'VERSION').write_text(value + '\n', encoding='utf-8')
                self.assertEqual(release.read_version(self.root), value)
        for value in ('01.2.3', '1.2', '1.2.3-01', '1.2.3-', '1.2.3+x', '../1.2.3', '1.2.3\nother'):
            with self.subTest(value=value):
                (self.root / 'VERSION').write_text(value, encoding='utf-8')
                with self.assertRaises(ValueError):
                    release.read_version(self.root)

    def test_tag_identity_and_commit(self):
        (self.root / 'VERSION').write_text(self.version, encoding='utf-8')
        with self.assertRaises(ValueError):
            release.validate_tag('v9.9.9', self.root)
        with patch.object(release.subprocess, 'check_output', side_effect=['head\n', 'head\n']):
            self.assertEqual(release.validate_tag('v1.2.3', self.root), 'head')
        with patch.object(release.subprocess, 'check_output', side_effect=['head\n', 'moved\n']):
            with self.assertRaises(ValueError):
                release.validate_tag('v1.2.3', self.root)

    def test_archives_preserve_notices_and_executable(self):
        for target in ('linux-x86_64-gcc', 'windows-x86_64-msvc'):
            with self.subTest(target=target):
                stage = self.root / target
                binary = stage / 'bin' / ('clutch.exe' if target.startswith('windows-') else 'clutch')
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b'native executable fixture')
                binary.chmod(0o755)
                notices = stage / 'share/licenses/clutch'
                notices.mkdir(parents=True)
                for name in release.NOTICES:
                    (notices / name).write_text(name, encoding='utf-8')
                with patch.object(release.subprocess, 'check_output', return_value='clutch 1.2.3\n'):
                    archive = release.package(stage, self.root / 'output', target, self.version)
                if archive.suffix == '.zip':
                    with zipfile.ZipFile(archive) as bundle:
                        names = bundle.namelist()
                else:
                    with tarfile.open(archive) as bundle:
                        names = bundle.getnames()
                        # Windows filesystems cannot model POSIX executable bits.
                        if os.name != 'nt':
                            self.assertTrue(bundle.getmember('clutch-1.2.3/bin/clutch').mode & 0o111)
                self.assertIn(f'clutch-1.2.3/bin/{binary.name}', names)
                for name in release.NOTICES:
                    self.assertIn(f'clutch-1.2.3/share/licenses/clutch/{name}', names)
                self.assertEqual(len(names), 1 + len(release.NOTICES))

    def test_package_rejects_missing_notice_and_wrong_binary_version(self):
        with self.assertRaises(ValueError):
            release.package(self.root, self.root / 'output', 'linux-x86_64-gcc', self.version)
        (self.root / 'bin').mkdir()
        (self.root / 'bin/clutch').write_bytes(b'binary')
        notices = self.root / 'share/licenses/clutch'
        notices.mkdir(parents=True)
        for name in release.NOTICES:
            (notices / name).touch()
        with patch.object(release.subprocess, 'check_output', return_value='clutch 9.9.9\n'):
            with self.assertRaises(ValueError):
                release.package(self.root, self.root / 'output', 'linux-x86_64-gcc', self.version)

    def test_checksums_reject_tampering(self):
        self.assets()
        archive = self.root / release.archive_name(self.version, release.TARGETS[0])
        archive.write_bytes(b'changed after build')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            release.verify_assets(self.root, self.version)

    def test_missing_and_unexpected_assets(self):
        self.assets()
        unexpected = self.root / 'unreviewed.zip'
        unexpected.touch()
        with self.assertRaises(ValueError):
            release.verify_assets(self.root, self.version)
        unexpected.unlink()
        (self.root / release.archive_name(self.version, release.TARGETS[0])).unlink()
        with self.assertRaises(ValueError):
            release.verify_assets(self.root, self.version)

    def test_complete_checksums(self):
        self.assets()
        release.verify_assets(self.root, self.version)
        release.verify_assets(self.root, self.version)
        self.assertEqual(len((self.root / 'SHA256SUMS').read_text().splitlines()), len(release.TARGETS))

    def test_matrix_matches_supported_assets(self):
        workflow = (release.ROOT / '.github/workflows/build.yml').read_text(encoding='utf-8')
        import re
        self.assertCountEqual(re.findall(r'target: ([a-z0-9_-]+)', workflow), release.TARGETS)

    def test_published_release_cannot_be_overwritten(self):
        self.assets()
        view = subprocess.CompletedProcess([], 0, json.dumps({'isDraft': False, 'assets': []}))
        with patch.object(release, 'read_version', return_value=self.version), \
             patch.object(release, 'validate_tag'), \
             patch.object(release.subprocess, 'run', return_value=view) as run:
            with self.assertRaisesRegex(ValueError, 'already published'):
                release.publish(self.root, 'v1.2.3')
            self.assertEqual(run.call_count, 1)

    def test_upload_failure_leaves_draft_unpublished(self):
        self.assets()
        view = subprocess.CompletedProcess([], 0, json.dumps({'isDraft': True, 'assets': []}))
        with patch.object(release, 'read_version', return_value=self.version), \
             patch.object(release, 'validate_tag'), \
             patch.object(release.subprocess, 'run', side_effect=[view, subprocess.CalledProcessError(1, 'gh')]) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.root, 'v1.2.3')
            self.assertFalse(any('edit' in call.args[0] for call in run.call_args_list))

    def test_draft_publishes_only_with_all_assets(self):
        self.assets()
        names = {path.name for path in self.root.iterdir()} | {'SHA256SUMS'}
        view = subprocess.CompletedProcess([], 0, json.dumps({'isDraft': True, 'assets': []}))
        uploaded = json.dumps({'assets': [{'name': name} for name in names]})
        with patch.object(release, 'read_version', return_value=self.version), \
             patch.object(release, 'validate_tag') as validate, \
             patch.object(release.subprocess, 'run', side_effect=[view, None, None]) as run, \
             patch.object(release.subprocess, 'check_output', return_value=uploaded):
            release.publish(self.root, 'v1.2.3')
            self.assertEqual(validate.call_count, 2)
            self.assertIn('--draft=false', run.call_args.args[0])

    def test_incomplete_server_assets_prevent_publication(self):
        self.assets()
        view = subprocess.CompletedProcess([], 0, json.dumps({'isDraft': True, 'assets': []}))
        with patch.object(release, 'read_version', return_value=self.version), \
             patch.object(release, 'validate_tag'), \
             patch.object(release.subprocess, 'run', side_effect=[view, None]) as run, \
             patch.object(release.subprocess, 'check_output', return_value=json.dumps({'assets': []})):
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                release.publish(self.root, 'v1.2.3')
            self.assertFalse(any('edit' in call.args[0] for call in run.call_args_list))


if __name__ == '__main__':
    unittest.main()
