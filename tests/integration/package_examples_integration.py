#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build the shipped package examples; network/CUDA checks require explicit flags."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

EXAMPLES = Path(__file__).resolve().parents[2] / 'examples/dependencies'


class PackageExamples(unittest.TestCase):
    """Use real example manifests and consumer tests instead of generated copies."""

    def setUp(self):
        work = None
        if os.name == 'nt':
            # MSBuild deliberately excludes TEMP from dependency read tracking.
            work = Path(__file__).resolve().parents[1] / 'work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='package examples ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve() / 'dependencies'

        def ignore(folder, names):
            """Keep published inputs, excluding prepared machine-specific state."""
            omitted = set(shutil.ignore_patterns('Clutch-build-*', '.clutch', '__pycache__')(folder, names))
            if Path(folder).name in ('git-local', 'catalog'):
                omitted.add('clutch.lock')
                if Path(folder).name == 'catalog':
                    omitted.add('clutch.toml')
            return omitted

        shutil.copytree(EXAMPLES, self.root, ignore=ignore)

    def call(self, project, *arguments, success=True):
        """Retain stdout/stderr when a shipped example fails to build or run."""
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=project,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, project, *extra):
        """Pin real host compilers while using the sample's own build profiles."""
        return self.call(project, '.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                         '--build-tool', OPTIONS.ninja, *extra)

    def exercise(self, project, expected):
        """Exercise both profiles, independent consumer tests and frozen offline use."""
        for profile in ((), ('--release',)):
            with self.subTest(project=project.name, profile=profile):
                self.configure(project, *profile)
                self.assertIn(expected, self.call(project, 'run', *profile, '--jobs', '2'))
                self.assertIn('1 passed', self.call(project, 'test', *profile, '--jobs', '2'))
                before = (project / 'clutch.lock').read_bytes()
                self.configure(project, *profile, '--locked', '--offline')
                self.assertEqual(before, (project / 'clutch.lock').read_bytes())
                self.assertIn(expected, self.call(project, 'run', *profile, '--jobs', '2'))

    def prepare(self):
        """Publish local repositories without requiring any hosted Git service."""
        result = subprocess.run([sys.executable, str(self.root / 'prepare.py')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_local_directory_and_installed_package(self):
        """The default example needs neither a remote repository nor an SDK install."""
        self.exercise(self.root, 'Local packages: 42; installed SDK: 1.0.0')
        lock = (self.root / 'clutch.lock').read_text(encoding='utf-8')
        self.assertIn('provider = "path"', lock)
        self.assertIn('provider = "system"', lock)
        self.assertFalse((self.root / '.clutch/dependency-cache.toml').exists())

    def test_explicit_system_interface(self):
        """SDK headers can be consumed without any package/build-system metadata."""
        project = self.root / 'system-interface'
        self.exercise(project, 'System interface: 1.0.0')
        self.assertFalse((project / 'Clutch-build-debug/.clutch/foreign').exists())

    @unittest.skipIf(os.name == 'nt', 'Makefile sample uses GNU-driver flags and ar')
    def test_makefile_package(self):
        """The shipped Makefile keeps objects/libraries outside its live sources."""
        if not (shutil.which('gmake') or shutil.which('make')):
            self.skipTest('GNU Make is not installed')
        self.exercise(self.root / 'makefile', 'Git package: 42')
        self.assertFalse((self.root / 'local/number/number.o').exists())
        self.assertFalse((self.root / 'local/number/libnumber.a').exists())

    def test_local_git_and_named_catalog(self):
        """Prepared Git/catalog samples are pinned, idempotent and build offline."""
        self.prepare()
        manifest = self.root / 'catalog/clutch.toml'
        prepared = manifest.read_bytes()
        self.prepare()
        self.assertEqual(prepared, manifest.read_bytes())
        for name in ('git-local', 'catalog'):
            project = self.root / name
            self.exercise(project, 'Git package: 42')
            lock = (project / 'clutch.lock').read_text(encoding='utf-8')
            self.assertIn('refs/tags/v1.0.0', lock)
            self.assertFalse(list(project.rglob('vcpkg.exe')))
            self.assertFalse(list(project.rglob('installed')))

    def test_prepare_preserves_unowned_files(self):
        """The preparation helper cannot overwrite arbitrary generated directories."""
        directory = self.root / 'Clutch-build-fixtures'
        directory.mkdir()
        sentinel = directory / 'keep.txt'
        sentinel.write_text('unrelated content', encoding='utf-8')
        result = subprocess.run([sys.executable, str(self.root / 'prepare.py')],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unowned', result.stderr)
        self.assertEqual(sentinel.read_text(encoding='utf-8'), 'unrelated content')

    @unittest.skipUnless(os.name == 'nt', 'DirectX needs the Windows SDK')
    def test_directx_installed_package(self):
        """Link the real Windows SDK without opening a window or creating a GPU."""
        if Path(OPTIONS.cxx).stem.lower() not in ('cl', 'clang-cl'):
            self.skipTest('DirectX example requires an MSVC/clang-cl SDK environment')
        self.exercise(self.root / 'directx', 'Installed DirectX: d3d12')

    def test_github_header_only_package(self):
        """Opt-in smoke test uses the real pinned CLI11 GitHub release."""
        if not OPTIONS.network:
            self.skipTest('enable --network to fetch real GitHub packages')
        project = self.root / 'github-headers'
        self.exercise(project, 'GitHub CLI11: 42')
        self.assertIn('GitHub CLI11: 7', self.call(project, 'run', '--', '--value', '7'))

    def test_git_cmake_package(self):
        """Opt-in smoke test fetches and compiles fmt's real upstream CMake target."""
        if not OPTIONS.network:
            self.skipTest('enable --network to fetch real GitHub packages')
        self.exercise(self.root / 'git-cmake', 'Git CMake fmt: 42')

    def test_cuda_installed_package(self):
        """Explicit CUDA coverage needs a compatible preinstalled CUDA Toolkit."""
        if not OPTIONS.cuda:
            self.skipTest('enable --cuda when the CUDA Toolkit is installed')
        self.exercise(self.root / 'cuda', 'Installed CUDA Toolkit:')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + field, required=True)
    parser.add_argument('--network', action='store_true', help='also test real GitHub downloads')
    parser.add_argument('--cuda', action='store_true', help='also test an installed CUDA Toolkit')
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
