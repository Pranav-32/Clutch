#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise CMake's version generator against real, disposable Git repositories."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('git') and shutil.which('cmake'), 'Git and CMake are required')
class GitVersionTests(unittest.TestCase):
    """No tags or commits are ever created in the developer's actual checkout."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch version ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'src/core').mkdir(parents=True)
        shutil.copy2(ROOT / 'src/core/version.hpp.in', self.source / 'src/core/version.hpp.in')
        (self.source / 'VERSION').write_text('1.2.3\n', encoding='utf-8')
        (self.source / '.gitignore').write_text('/ignored.txt\n', encoding='utf-8')
        self.git('init', '-q')
        self.git('config', 'user.name', 'Clutch version test')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'commit.gpgsign', 'false')
        self.git('config', 'tag.gpgsign', 'false')
        self.git('config', 'core.autocrlf', 'false')
        self.git('add', '.')
        self.git('commit', '-qm', 'feat: version fixture')

    def git(self, *arguments):
        """Capture failures as test failures without relying on shell quoting."""
        return subprocess.check_output(['git', '-C', str(self.source), *arguments], text=True).strip()

    def version(self, *options):
        """Invoke the exact script used by ordinary native Clutch builds."""
        build = self.root / 'build'
        subprocess.run(['cmake', f'-DCLUTCH_SOURCE_DIR={self.source}',
                        f'-DCLUTCH_BINARY_DIR={build}', *options,
                        '-P', str(ROOT / 'cmake/GenerateVersion.cmake')], check=True)
        version = (build / 'generated/version.txt').read_text().strip()
        header = (build / 'generated/core/version.hpp').read_text()
        self.assertIn(version, header)
        return version

    def test_clean_release_tag(self):
        self.git('tag', '-a', 'v1.2.3', '-m', 'fixture release')
        self.assertEqual(self.version(), '1.2.3')

    def test_development_commit(self):
        self.assertRegex(self.version(), r'^1\.2\.3-dev\+g[0-9a-f]{12,}$')

    def test_tracked_and_untracked_edits(self):
        self.git('tag', 'v1.2.3')
        (self.source / 'VERSION').write_text('1.2.3\n\n', encoding='utf-8')
        self.assertTrue(self.version().endswith('.dirty'))
        self.git('restore', 'VERSION')
        (self.source / 'new.cpp').touch()
        self.assertTrue(self.version().endswith('.dirty'))

    def test_ignored_files_do_not_dirty_release(self):
        self.git('tag', 'v1.2.3')
        (self.source / 'ignored.txt').touch()
        self.assertEqual(self.version(), '1.2.3')

    def test_mismatched_tag_is_development(self):
        self.git('tag', 'v9.9.9')
        self.assertIn('-dev+g', self.version())

    def test_prerelease_tag_and_development_identity(self):
        (self.source / 'VERSION').write_text('1.2.3-rc.1\n', encoding='utf-8')
        self.git('add', 'VERSION')
        self.git('commit', '-qm', 'chore: prerelease fixture')
        self.assertRegex(self.version(), r'^1\.2\.3-rc\.1\.dev\+g[0-9a-f]{12,}$')
        self.git('tag', 'v1.2.3-rc.1')
        self.assertEqual(self.version(), '1.2.3-rc.1')

    def test_no_git_or_source_archive(self):
        self.assertEqual(self.version('-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE'), '1.2.3+source')
        archive = self.root / 'archive'
        shutil.copytree(self.source, archive, ignore=shutil.ignore_patterns('.git'))
        self.source = archive
        self.assertEqual(self.version(), '1.2.3+source')

    def test_shallow_checkout_is_identified(self):
        shallow = self.root / 'shallow'
        subprocess.run(['git', 'clone', '-q', '--depth', '1', self.source.as_uri(), str(shallow)], check=True)
        self.source = shallow
        self.assertTrue(self.version().endswith('.shallow'))

    def test_worktree_is_supported(self):
        worktree = self.root / 'worktree'
        self.git('worktree', 'add', '-q', '--detach', str(worktree))
        self.source = worktree
        self.assertIn('-dev+g', self.version())

    def test_unchanged_identity_preserves_header_timestamp(self):
        first = self.version()
        header = self.root / 'build/generated/core/version.hpp'
        before = header.stat().st_mtime_ns
        self.assertEqual(self.version(), first)
        self.assertEqual(header.stat().st_mtime_ns, before)
        self.git('tag', 'v1.2.3')
        self.assertEqual(self.version(), '1.2.3')

    def test_invalid_base_version_is_rejected(self):
        (self.source / 'VERSION').write_text('1.2.3-01\n', encoding='utf-8')
        with self.assertRaises(subprocess.CalledProcessError):
            self.version()


if __name__ == '__main__':
    unittest.main()
