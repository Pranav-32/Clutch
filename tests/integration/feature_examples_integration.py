#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build shipped feature examples from isolated copies, without network access."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

import watch_integration as watch_harness

ROOT = Path(__file__).resolve().parents[2]
TIMEOUT = 120


class FeatureExamples(unittest.TestCase):
    """Keep checked-in examples executable rather than testing rewritten manifests."""

    def setUp(self):
        parent = ROOT / 'tests/work' if os.name == 'nt' else None
        if parent:
            parent.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='feature examples ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def copy(self, family):
        """Copy sources, excluding local configuration products and caches."""
        destination = self.root / family
        shutil.copytree(ROOT / 'examples' / family, destination,
                        ignore=shutil.ignore_patterns('Clutch-build-*', '.clutch',
                                                     'clutch.lock', '__pycache__'))
        return destination

    def call(self, project, *arguments, success=True):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=project,
                                capture_output=True, text=True, timeout=TIMEOUT)
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        return result, output

    def configure(self, project, release=False, success=True):
        """Leave the generator omitted so automatic executor selection is exercised."""
        return self.call(project, '.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                         '--build-tool', OPTIONS.ninja,
                         *(['--release'] if release else []), success=success)

    def run_example(self, project, expected, release=False, target=None):
        flags = ['--release'] if release else []
        if target:
            flags += ['--target', target]
        self.configure(project, release)
        _, output = self.call(project, 'run', *flags)
        self.assertIn(expected, output)
        return output

    def test_configured_files_and_versions(self):
        """Render variables, escaped tokens and a file-owned base version."""
        project = self.copy('configured-files')
        for release in (False, True):
            with self.subTest(release=release):
                output = self.run_example(project, 'base=1.2.3; API=3', release)
                self.assertIn('Configured by Clutch', output)
                self.assertIn('Literal token: @PROJECT_VERSION@', output)

    def test_source_discovery_and_patterns(self):
        """Flat patterns omit the deeper unit; implicit and recursive scans find it."""
        family = self.copy('discovery')
        for variant, count in (('.', 4), ('flat', 3), ('recursive', 4)):
            with self.subTest(variant=variant):
                project = family / variant
                self.run_example(project, 'Discovered: 42')
                rows = json.loads((project / 'Clutch-build-debug/compile_commands.json').read_text())
                self.assertEqual(len(rows), count)
                self.assertEqual({Path(row['file']).suffix for row in rows}, {'.c', '.cpp'})
                self.assertTrue(all(Path(row['directory']).is_absolute() for row in rows))

    @unittest.skipUnless(shutil.which('git'), 'Git identity requires Git')
    def test_configured_files_git_identity(self):
        """Follow the documented clean-tag workflow, including the generated lock."""
        project = self.copy('configured-files')
        self.configure(project)

        def git(*arguments):
            result = subprocess.run(['git', *arguments], cwd=project, capture_output=True,
                                    text=True, timeout=TIMEOUT)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout.strip()

        git('init')
        git('add', '.')
        git('-c', 'user.name=Example Test', '-c', 'user.email=example@example.invalid',
            '-c', 'commit.gpgsign=false', 'commit', '-m', 'Add configured-file example')
        git('tag', 'v1.2.3')
        _, output = self.call(project, 'run')
        self.assertIn('configured_example 1.2.3; base=1.2.3', output)
        self.assertEqual(git('status', '--porcelain'), '')

    def test_independent_profiles(self):
        """Compile both optimization/symbol combinations and warning-array manifests."""
        family = self.copy('profiles')
        for variant in ('.', 'no-symbols'):
            for release in (False, True):
                with self.subTest(variant=variant, release=release):
                    self.run_example(family / variant, 'Profile example: 42', release)

    def test_shared_library_and_tests(self):
        """Exercise exports, consumer linkage, runtime loading and clean rebuilds."""
        project = self.copy('shared-library')
        for release in (False, True):
            with self.subTest(release=release):
                self.run_example(project, 'Shared answer: 42', release, 'hello')
                flags = ['--release'] if release else []
                self.call(project, 'test', *flags)
                _, output = self.call(project, 'run', '--target', 'hello', '--clean', *flags)
                self.assertIn('Shared answer: 42', output)

    def test_watched_application_and_resource_edits(self):
        """Run the shipped watch project and observe a real resource-only edit."""
        project = self.copy('watch')
        self.run_example(project, 'Resource: Hello from the watched resource', target='hello')
        self.call(project, 'test', '--target', 'message-test')
        session = watch_harness.Session(project, ['--run', '--target', 'hello',
                                                '--path', 'assets', '--ignore', 'assets/generated'])
        self.addCleanup(session.stop)
        session.cycle(1)
        session.quiet()
        (project / 'assets/message.txt').write_text('An edited resource\n', encoding='utf-8')
        session.cycle(2)
        session.text('Resource: An edited resource')
        session.quiet()
        (project / 'assets/generated/README.txt').write_text('Ignored edit\n', encoding='utf-8')
        session.quiet()

    def test_source_bundle_relocation(self):
        """Exclude generated resources, then rebuild the extracted source snapshot."""
        project = self.copy('watch')
        archive = self.root / 'watch-sources.zip'
        self.call(project, 'bundle', '--output', str(archive), '--exclude', 'assets/generated')
        destination = self.root / 'unpacked'
        with zipfile.ZipFile(archive) as source:
            self.assertFalse(any('assets/generated' in name for name in source.namelist()))
            source.extractall(destination)
        self.run_example(destination, 'Resource: Hello from the watched resource', target='hello')
        self.call(destination, 'test')

    def test_native_and_shell_commands(self):
        """Manual commands need their chosen runtime, not a compiler configuration."""
        family = self.copy('commands')
        if shutil.which('git'):
            _, output = self.call(family / 'native', 'exec', 'git-version')
            self.assertIn('git version', output)
        variant = 'shell-windows' if os.name == 'nt' else 'shell-posix'
        _, output = self.call(family / variant, 'exec', 'hello')
        self.assertIn('Hello from the', output)

    @unittest.skipUnless(shutil.which('node'), 'Node.js is an optional example runtime')
    def test_node_commands_and_hook(self):
        project = self.copy('commands') / 'node'
        _, output = self.call(project, 'exec', 'hello', '--', 'two words')
        self.assertIn('two words', output)
        self.configure(project)
        _, output = self.call(project, 'build')
        self.assertIn('Clutch-build-debug', output)

    def test_disabled_safety_profiles(self):
        project = self.copy('safety')
        for release in (False, True):
            with self.subTest(release=release):
                self.run_example(project, 'Safety example: 42', release)

    def test_optional_safety_variants(self):
        """Reject unavailable capabilities explicitly; never silently drop requested checks."""
        family = self.copy('safety')
        for variant in ('address', 'undefined', 'leak', 'combined', 'thread', 'lifetime'):
            with self.subTest(variant=variant):
                if os.name == 'nt' and variant in ('leak', 'thread'):
                    self.skipTest('this sanitizer example requires a supported POSIX runtime')
                project = family / variant
                result, output = self.configure(project, release=True, success=False)
                if result.returncode:
                    self.assertIn('safety', output.lower(), output)
                    self.assertTrue(any(word in output.lower() for word in
                                        ('unavailable', 'unsupported', 'not support', 'supports only')), output)
                    self.skipTest('selected compiler/runtime explicitly rejects ' + variant)
                self.call(project, 'build', '--release')
                result, output = self.call(project, 'run', '--release', success=False)
                if result.returncode and variant == 'thread' and any(message in output for message in
                        ('unexpected memory mapping', 'ADDR_NO_RANDOMIZE', 'personality(',
                         'incompatible memory layout but was unable to disable ASLR')):
                    self.skipTest('ThreadSanitizer cannot reserve its address space on this host')
                self.assertEqual(result.returncode, 0, output)
                self.assertIn('Safety example: 42', output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + field, required=True)
    OPTIONS, remaining = parser.parse_known_args()
    # Preserve compiler driver names: resolving a clang++ symlink changes behavior.
    for field in ('clutch', 'cc', 'cxx', 'ninja'):
        setattr(OPTIONS, field, os.path.abspath(shutil.which(getattr(OPTIONS, field)) or
                                               getattr(OPTIONS, field)))
    watch_harness.OPTIONS = OPTIONS
    unittest.main(argv=[sys.argv[0], *remaining])
