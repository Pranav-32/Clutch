#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Independently read ZIP snapshots and rebuild relocated source dependencies."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

COMMAND_TIMEOUT = 120
STRESS_FILES = 256


class BundleTests(unittest.TestCase):
    """Exercise the installed CLI with real archives, repositories and compilers."""

    def setUp(self):
        work = None
        if os.name == 'nt':
            work = Path(__file__).resolve().parents[1] / 'work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='bundle ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.app = self.root / 'original'
        self.write(self.app, 'clutch.toml', '[project]\nname="demo"\nversion="1.0.0"\n'
                   'std=17\nentry="main.cpp"\n[profile.debug]\nbuild_dir="out"\n')
        self.write(self.app, 'main.cpp', '#include <cstdio>\nint main() { std::puts("snapshot works"); }\n')

    def write(self, root, relative, text):
        """Create UTF-8 fixture files, never changing production inputs."""
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode('utf-8'))

    def cli(self, *arguments, root=None, success=True):
        """Bound child lifetime and preserve the original command diagnostics."""
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=root or self.app,
                                capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, root):
        return self.cli('.', '--offline', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        '--generator', 'ninja', '--build-tool', OPTIONS.ninja, root=root)

    def bundle(self, output='sources.zip', *extra, success=True):
        archive = self.root / output
        result = self.cli('bundle', '--output', str(archive), *extra, success=success)
        return archive, result

    def extract(self, archive):
        """Use Python's independent ZIP/CRC reader rather than Clutch's encoder."""
        target = self.root / 'relocated workspace'
        with zipfile.ZipFile(archive) as snapshot:
            self.assertIsNone(snapshot.testzip())
            snapshot.extractall(target)
        return target

    def test_repeatable_source_zip_and_exclusions(self):
        """Host output trees stay out and source timestamps do not affect bytes."""
        self.write(self.app, '.git/config', 'private repository metadata\n')
        self.write(self.app, '.clutch/unrelated', 'private cache\n')
        self.write(self.app, 'old-build/.clutch-build-owner', 'owned\n')
        self.write(self.app, 'old-build/generated.o', 'not source\n')
        self.write(self.app, 'cmake-output/CMakeCache.txt', 'cache\n')
        self.write(self.app, 'private/omit.txt', 'explicit exclusion\n')
        self.write(self.app, 'LICENSE', 'Example license retained\n')
        self.write(self.app, 'data/café.txt', 'Unicode content: λ\n')
        (self.app / 'empty').mkdir()
        first, _ = self.bundle('first.zip', '--exclude', 'private')
        os.utime(self.app / 'main.cpp', (1_600_000_000, 1_600_000_000))
        second, _ = self.bundle('second.zip', '--exclude', 'private')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as snapshot:
            self.assertIsNone(snapshot.testzip())
            names = snapshot.namelist()
            self.assertIn('LICENSE', names)
            self.assertIn('empty/', names)
            self.assertEqual(snapshot.read('data/café.txt').decode(), 'Unicode content: λ\n')
            self.assertFalse(any(name.startswith(('.git/', 'old-build/', 'cmake-output/', 'private/')) for name in names))
            self.assertNotIn('.clutch/unrelated', names)
            self.assertNotIn('clutch.lock', names)
            self.assertIn('.clutch/bundle/snapshot.toml', names)
            self.assertTrue(all(info.compress_type == zipfile.ZIP_STORED for info in snapshot.infolist()))
        relocated = self.extract(first)
        self.configure(relocated)
        self.assertIn('snapshot works', self.cli('run', root=relocated))

    @unittest.skipUnless(shutil.which('git') and shutil.which('cmake'), 'Git and CMake are required for source dependency relocation')
    def test_git_local_cmake_dependencies_rebuild_after_originals_move(self):
        """Snapshot transitive imports, then remove their original locations."""
        repository = self.root / 'git headers'
        self.write(repository, 'include/answer.hpp', '#pragma once\n#define ANSWER 42\n')
        for arguments in [('init', '-q'), ('config', 'user.name', 'Clutch Tests'),
                          ('config', 'user.email', 'tests@example.invalid'), ('add', '.'),
                          ('-c', 'commit.gpgsign=false', 'commit', '-qm', 'Publish fixture'),
                          ('-c', 'tag.gpgsign=false', 'tag', 'v1.0.0')]:
            subprocess.run(['git', *arguments], cwd=repository, check=True,
                           capture_output=True, timeout=COMMAND_TIMEOUT)
        library = self.root / 'local math'
        self.write(library, 'value.cpp', '#include <answer.hpp>\nint value() { return ANSWER; }\n')
        self.write(library, 'clutch.toml', '[project]\nname="math"\nversion="1.0.0"\nstd=17\n'
                   '[[target]]\nname="math"\ntype="static"\nsources=["value.cpp"]\n'
                   '[dependencies]\nheaders={git=' + json.dumps(repository.as_posix()) +
                   ',tag="v1.0.0",build_system="header-only",include_dirs=["include"]}\n')
        foreign = self.root / 'foreign library'
        self.write(foreign, 'CMakeLists.txt', 'cmake_minimum_required(VERSION 3.21)\n'
                   'project(Foreign LANGUAGES CXX)\nadd_library(extra STATIC extra.cpp)\n')
        self.write(foreign, 'extra.cpp', 'int extra() { return 3; }\n')
        self.write(self.app, 'main.cpp', '#include <cstdio>\nint value(); int extra();\n'
                   'int main() { const int result=value()+extra(); std::printf("%d\\n",result); return result==45?0:1; }\n')
        with (self.app / 'clutch.toml').open('a', encoding='utf-8') as stream:
            stream.write('[dependencies]\nmath={path="../local math",target="math"}\n'
                         'extra={path="../foreign library",build_system="cmake",target="extra"}\n')
        # Initial configuration deliberately permits fetching a local Git ref.
        self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx, '--generator', 'ninja', '--build-tool', OPTIONS.ninja)
        original_lock = (self.app / 'clutch.lock').read_bytes()
        archive, _ = self.bundle()
        self.assertEqual(original_lock, (self.app / 'clutch.lock').read_bytes())
        relocated = self.extract(archive)
        for source in (self.app, library, foreign, repository):
            source.rename(source.with_name(source.name + '-unavailable'))
        self.configure(relocated)
        self.assertIn('45', self.cli('run', '--jobs', '4', root=relocated))
        lock = (relocated / 'clutch.lock').read_text(encoding='utf-8')
        self.assertNotIn('provider = "git"', lock)
        self.cli('.', '--offline', '--locked', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                 '--build-tool', OPTIONS.ninja, root=relocated)

    def test_workspace_members_and_absolute_source_paths_relocate(self):
        """Retain member relationships while rewriting producer-specific paths."""
        self.write(self.app, 'clutch.toml', '[workspace]\nmembers=["hello"]\n')
        self.write(self.app, 'hello/clutch.toml', '[project]\nname="hello"\nversion="1.0.0"\nstd=17\nentry=' +
                   json.dumps((self.app / 'main.cpp').as_posix()) + '\n')
        archive, _ = self.bundle()
        relocated = self.extract(archive)
        self.app.rename(self.app.with_name('original-unavailable'))
        self.configure(relocated)
        self.assertIn('snapshot works', self.cli('run', root=relocated))

    def test_failed_bundle_preserves_outputs_and_required_sources(self):
        """A failed snapshot cannot replace an archive or omit its entry point."""
        output = self.root / 'existing.zip'
        output.write_bytes(b'keep existing archive')
        self.bundle('existing.zip', success=False)
        self.assertEqual(output.read_bytes(), b'keep existing archive')
        for exclude in ('main.cpp', 'clutch.toml', '../outside'):
            with self.subTest(exclude=exclude):
                archive, _ = self.bundle('failed.zip', '--exclude', exclude, success=False)
                self.assertFalse(archive.exists())
        self.assertFalse(list(self.root.glob('.clutch-zip-*')))

    def test_recursive_sources_pch_and_in_tree_dependencies_relocate(self):
        """Wildcard paths and in-tree packages keep their meaning after extraction."""
        self.write(self.app, 'clutch.toml', '[project]\nname="demo"\nversion="1.0.0"\nstd=17\n'
                   '[build]\nsources=["src/*.{cpp,hpp}"]\npch=["include/prefix.hpp"]\n'
                   '[dependencies]\nheaders={path="vendor/headers",build_system="header-only"}\n')
        self.write(self.app, 'src/main.cpp', '#include <cstdio>\nint value();\n'
                   'int main() { std::printf("%d\\n",value()); return value()==42?0:1; }\n')
        self.write(self.app, 'src/value.cpp', 'int value() { return ANSWER; }\n')
        self.write(self.app, 'src/value.hpp', '#pragma once\nint value();\n')
        self.write(self.app, 'include/prefix.hpp', '#pragma once\n#include <answer.hpp>\n')
        self.write(self.app, 'vendor/headers/include/answer.hpp', '#pragma once\n#define ANSWER 42\n')
        archive, _ = self.bundle()
        relocated = self.extract(archive)
        self.app.rename(self.app.with_name('original-unavailable'))
        self.configure(relocated)
        self.assertIn('42', self.cli('run', '--jobs', '4', root=relocated))

    def test_concurrent_publish_has_one_complete_winner(self):
        """Racing processes cannot overwrite or partially publish another archive."""
        archive = self.root / 'race.zip'
        def publish(_):
            return subprocess.run([OPTIONS.clutch, 'bundle', '--output', str(archive)],
                                  cwd=self.app, capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(publish, range(8)))
        self.assertEqual(sum(result.returncode == 0 for result in results), 1,
                         '\n'.join(result.stdout + result.stderr for result in results))
        with zipfile.ZipFile(archive) as snapshot:
            self.assertIsNone(snapshot.testzip())
            self.assertIn('clutch.toml', snapshot.namelist())
        self.assertFalse(list(self.root.glob('.clutch-zip-*')))

    def test_redirected_files_are_rejected(self):
        """Links must not pull arbitrary machine files into a source archive."""
        self.write(self.root, 'outside.txt', 'outside source\n')
        try:
            (self.app / 'redirect.txt').symlink_to(self.root / 'outside.txt')
        except OSError:
            self.skipTest('host does not permit symbolic-link creation')
        archive, _ = self.bundle(success=False)
        self.assertFalse(archive.exists())

    def test_empty_external_dependency_directory_survives_extraction(self):
        """Even a header package with no files must keep its declared root."""
        (self.root / 'empty package').mkdir()
        with (self.app / 'clutch.toml').open('a', encoding='utf-8') as stream:
            stream.write('[dependencies]\nempty={path="../empty package",build_system="header-only"}\n')
        archive, _ = self.bundle()
        relocated = self.extract(archive)
        self.configure(relocated)
        self.assertIn('snapshot works', self.cli('run', root=relocated))

    def test_exclusions_cannot_remove_test_or_pch_inputs(self):
        """A snapshot must retain inputs of all declared targets and tests."""
        self.write(self.app, 'prefix.hpp', '#pragma once\n')
        self.write(self.app, 'checks/check.cpp', 'int main() { return 0; }\n')
        with (self.app / 'clutch.toml').open('a', encoding='utf-8') as stream:
            stream.write('[build]\npch=["prefix.hpp"]\n'
                         '[[test]]\nname="check"\nsources=["checks/check.cpp"]\n')
        for exclusion in ('prefix.hpp', 'checks/check.cpp'):
            archive, message = self.bundle('failed.zip', '--exclude', exclusion, success=False)
            self.assertIn('required source or include', message)
            self.assertFalse(archive.exists())

    def test_external_installed_prefix_requires_portable_discovery(self):
        """SDK installations are prerequisites, not implicit redistribution roots."""
        sdk = self.root / 'installed sdk'
        self.write(sdk, 'include/sdk.hpp', '#pragma once\n')
        with (self.app / 'clutch.toml').open('a', encoding='utf-8') as stream:
            stream.write('[dependencies]\nsdk={system="example",root=' +
                         json.dumps(sdk.as_posix()) + ',include_dirs=["include"]}\n')
        archive, message = self.bundle(success=False)
        self.assertIn('outside the project and dependency source trees', message)
        self.assertFalse(archive.exists())

    def test_large_source_snapshot_crc_and_default_output(self):
        """Check streamed binary CRCs and many entries without a timing threshold."""
        payload = bytes(range(256)) * 4096
        (self.app / 'payload.bin').write_bytes(payload)
        for index in range(STRESS_FILES):
            self.write(self.app, f'data/group{index % 16}/file{index}.txt', f'source {index}\n')
        self.cli('bundle')
        archive = self.app / '.clutch/bundles/demo-sources.zip'
        with zipfile.ZipFile(archive) as snapshot:
            self.assertIsNone(snapshot.testzip())
            self.assertEqual(snapshot.read('payload.bin'), payload)
            for index in range(STRESS_FILES):
                self.assertEqual(snapshot.read(f'data/group{index % 16}/file{index}.txt').decode(), f'source {index}\n')
            self.assertFalse(any(name.startswith('.clutch/bundles/') for name in snapshot.namelist()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + field, required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
