#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise packaging/install.sh offline with release-shaped local archives."""

import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '.github/scripts'))
import release

SHELL = shutil.which('sh')
VERSION = '1.2.3'
TARGET = 'linux-x86_64-gcc'


@unittest.skipUnless(SHELL, 'a POSIX sh is required to test install.sh')
class InstallScriptTests(unittest.TestCase):
    """Never download; a fake uname pins the Linux x86_64 target."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.home = self.root / 'home'
        self.home.mkdir()
        fake = self.root / 'fake-bin'
        fake.mkdir()
        uname = fake / 'uname'
        uname.write_bytes(b'#!/bin/sh\ncase "$1" in -s) echo Linux ;; -m) echo x86_64 ;; *) echo Linux ;; esac\n')
        uname.chmod(0o755)
        self.environment = dict(os.environ, HOME=self.home.as_posix(), SHELL='/bin/bash',
                                PATH=str(fake) + os.pathsep + os.environ.get('PATH', ''))
        self.archive = self.root / release.archive_name(VERSION, TARGET)
        with tarfile.open(self.archive, 'w:gz') as bundle:
            self.add(bundle, f'clutch-{VERSION}/bin/clutch',
                     f'#!/bin/sh\necho "clutch {VERSION}"\n'.encode(), 0o755)
            for notice in release.NOTICES:
                self.add(bundle, f'clutch-{VERSION}/share/licenses/clutch/{notice}', b'notice\n', 0o644)
        release.write_checksum(self.archive)

    @staticmethod
    def add(bundle, name, content, mode):
        info = tarfile.TarInfo(name)
        info.size = len(content)
        info.mode = mode
        bundle.addfile(info, io.BytesIO(content))

    def install(self, *arguments):
        script = (release.ROOT / 'packaging/install.sh').as_posix()
        return subprocess.run([SHELL, script, '--archive', self.archive.as_posix(), *arguments],
                              env=self.environment, capture_output=True, text=True)

    def test_installs_verified_archive_without_touching_profiles(self):
        prefix = self.root / 'prefix'
        result = self.install('--prefix', prefix.as_posix())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'Installed clutch {VERSION}', result.stdout)
        self.assertTrue((prefix / 'bin/clutch').is_file())
        self.assertTrue((prefix / 'share/licenses/clutch/LICENSE').is_file())
        self.assertIn('rerun with --modify-path', result.stdout)
        self.assertEqual(list(self.home.iterdir()), [])

    def test_checksum_mismatch_installs_nothing(self):
        sidecar = self.archive.with_name(self.archive.name + '.sha256')
        sidecar.write_text('0' * 64 + f'  {self.archive.name}\n', encoding='utf-8')
        prefix = self.root / 'prefix'
        result = self.install('--prefix', prefix.as_posix())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('SHA-256 mismatch', result.stderr)
        self.assertFalse((prefix / 'bin/clutch').exists())

    def test_checksum_for_another_file_is_rejected(self):
        sidecar = self.archive.with_name(self.archive.name + '.sha256')
        digest = sidecar.read_text(encoding='utf-8').split()[0]
        sidecar.write_text(f'{digest}  other.tar.gz\n', encoding='utf-8')
        result = self.install('--prefix', (self.root / 'prefix').as_posix())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum file names', result.stderr)

    def test_modify_path_appends_profile_line_once(self):
        prefix = self.root / 'prefix'
        for _ in range(2):
            result = self.install('--prefix', prefix.as_posix(), '--modify-path')
            self.assertEqual(result.returncode, 0, result.stderr)
        profile = (self.home / '.bashrc').read_text(encoding='utf-8')
        self.assertEqual(profile.count('export PATH='), 1)
        self.assertIn('/bin:$PATH"', profile)

    def test_unknown_option_fails(self):
        result = self.install('--bogus')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unknown option', result.stderr)


if __name__ == '__main__':
    unittest.main()
