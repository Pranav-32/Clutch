#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise bounded package graphs and real parallel C/C++ PCH builds offline."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

# Keep default CI runs small; larger local runs can explicitly increase these.
DEFAULT_PACKAGES = 48
DEFAULT_UNITS = 24
DEFAULT_REPEATS = 2
MAXIMUM_SCALE = 256
COMMAND_TIMEOUT = 120
# This is the documented resolver contract, exercised just above its boundary.
PACKAGE_LIMIT = 1024


def bounded_count(value):
    """Prevent accidental unbounded fixture sizes from command-line mistakes."""
    count = int(value)
    if not 1 <= count <= MAXIMUM_SCALE:
        raise argparse.ArgumentTypeError(f'count must be between 1 and {MAXIMUM_SCALE}')
    return count


class StressTests(unittest.TestCase):
    """Use real tools and isolated local sources, with no hosted dependencies."""

    def setUp(self):
        work = None
        if os.name == 'nt':
            work = Path(__file__).resolve().parents[1] / 'work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='stress ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.app = self.root / 'app'
        self.app.mkdir()
        self.write(self.app, 'main.cpp', 'int main() { return 0; }\n')

    def write(self, root, relative, text):
        """Create fixture inputs with explicit UTF-8 encoding."""
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def cli(self, *arguments, success=True):
        """Bound child lifetime and retain diagnostics without timing assertions."""
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.app,
                                capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, *arguments, success=True):
        """Use the host's real compilers and the same Ninja executor as normal CI."""
        return self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        '--build-tool', OPTIONS.ninja, *arguments, success=success)

    def manifest(self, dependencies):
        """Declare live header packages so resolution stress requires no Git service."""
        self.write(self.app, 'clutch.toml', '[project]\nname="stressapp"\nversion="1.0.0"\n'
                   'std=17\nentry="main.cpp"\n[build]\ngenerator="ninja"\n'
                   '[profile.debug]\nbuild_dir="out"\n[dependencies]\n' + dependencies)

    @staticmethod
    def dependency(path, version=None):
        """Keep all fixture path/value quoting valid on Windows and POSIX."""
        fields = {'path': path, 'build_system': 'header-only'}
        if version:
            fields['version'] = version
        return '{' + ','.join(f'{key}={json.dumps(value)}' for key, value in fields.items()) + '}'

    def test_shared_dependency_graph_reuse_and_conflict_rollback(self):
        """A wide diamond shares one leaf, preserves pins, and cannot publish a conflict."""
        leaf = self.root / 'packages/leaf'
        self.write(leaf, 'include/answer.hpp', '#pragma once\n#define SHARED_ANSWER 42\n')
        self.write(leaf, 'clutch.toml', '[project]\nname="leaf"\nversion="1.0.0"\n')
        declarations = []
        for index in range(OPTIONS.packages):
            branch = self.root / f'packages/branch{index}'
            self.write(branch, 'clutch.toml', '[dependencies]\nshared=' +
                       self.dependency('../leaf', '^1.0.0') + '\n')
            declarations.append(f'branch{index}=' + self.dependency(f'../packages/branch{index}'))
        self.write(self.app, 'main.cpp', '#include <answer.hpp>\n#include <cstdio>\n'
                   'int main() { std::printf("%d\\n", SHARED_ANSWER); return SHARED_ANSWER == 42 ? 0 : 1; }\n')
        self.manifest('\n'.join(declarations) + '\n')
        self.configure()
        lock_path = self.app / 'clutch.lock'
        lock = lock_path.read_bytes()
        self.assertEqual(lock.count(b'[[dependency]]'), OPTIONS.packages + 1)
        self.assertEqual(lock.count(b'source = "../packages/leaf"'), 1)
        self.assertFalse((self.app / '.clutch/deps').exists())
        self.cli('run', '--jobs', '4')
        output = self.app / ('out/stressapp.exe' if os.name == 'nt' else 'out/stressapp')
        stamp = output.stat().st_mtime_ns
        for _ in range(OPTIONS.repeats):
            self.configure('--locked', '--offline')
            self.assertEqual(lock, lock_path.read_bytes())
            self.assertIn('42', self.cli('run', '--jobs', '4'))
            self.assertEqual(stamp, output.stat().st_mtime_ns, 'unchanged resolution relinked')

        self.manifest('\n'.join(declarations) + '\nconflict=' +
                      self.dependency('../packages/leaf', '^2.0.0') + '\n')
        self.assertIn('version conflict', self.configure(success=False))
        self.assertEqual(lock, lock_path.read_bytes(), 'failed resolution changed the previous lock')

    def test_deep_chain_cycle_and_recovery(self):
        """Reject a long closing edge, then resolve the same graph after removing it."""
        for index in range(OPTIONS.packages):
            next_index = (index + 1) % OPTIONS.packages
            self.write(self.root / f'chain/p{index}', 'clutch.toml', '[dependencies]\nnext=' +
                       self.dependency(f'../p{next_index}') + '\n')
        self.manifest('first=' + self.dependency('../chain/p0') + '\n')
        self.assertIn('dependency cycle', self.configure(success=False))
        self.assertFalse((self.app / 'clutch.lock').exists())
        self.write(self.root / f'chain/p{OPTIONS.packages - 1}', 'clutch.toml', '# terminal package\n')
        self.configure()
        self.configure('--locked', '--offline')
        self.assertEqual((self.app / 'clutch.lock').read_bytes().count(b'[[dependency]]'), OPTIONS.packages)

    def test_package_limit_has_a_diagnostic_and_no_partial_lock(self):
        """Reject an oversized graph before recursion or metadata publication."""
        declarations = []
        for index in range(PACKAGE_LIMIT + 1):
            (self.root / f'limits/p{index}').mkdir(parents=True)
            declarations.append(f'p{index}=' + self.dependency(f'../limits/p{index}'))
        self.manifest('\n'.join(declarations) + '\n')
        self.assertIn('package/search limits', self.configure(success=False))
        self.assertFalse((self.app / 'clutch.lock').exists())
        self.assertFalse((self.app / 'out/configuration.toml').exists())

    def test_parallel_mixed_language_pch_and_noop_builds(self):
        """Serial and parallel cold builds must link every unit and reuse unchanged artifacts."""
        count = OPTIONS.units
        seed = 42
        self.write(self.app, 'include/seed.h', f'#pragma once\n#define SEED {seed}\n')
        self.write(self.app, 'include/prefix.h', '#pragma once\n#include <stdint.h>\n#include "seed.h"\n')
        self.write(self.app, 'include/prefix.hpp', '#pragma once\n#include <cstdint>\n#include "seed.h"\n')
        for index in range(count):
            suffix = 'c' if index % 2 == 0 else 'cpp'
            linkage = '' if suffix == 'c' else 'extern "C" '
            self.write(self.app, f'parts/u{index}.{suffix}',
                       f'{linkage}int part{index}(void) {{ return SEED + {index}; }}\n')
        declarations = ''.join(f'int part{index}(void);\n' for index in range(count))
        expression = '+'.join(f'part{index}()' for index in range(count))
        expected = count * seed + count * (count - 1) // 2
        self.write(self.app, 'main.cpp', '#include <cstdio>\nextern "C" {\n' + declarations + '}\n'
                   f'int main() {{ const int value={expression}; std::printf("%d\\n", value); '
                   f'return value == {expected} ? 0 : 1; }}\n')
        self.write(self.app, 'clutch.toml', '[project]\nname="stressapp"\nversion="1.0.0"\n'
                   'language="mixed"\nstd={c=11,cpp=17}\n[build]\ngenerator="ninja"\n'
                   'include_dirs=["include"]\npch={c=["include/prefix.h"],cpp=["include/prefix.hpp"]}\n'
                   '[profile.debug]\nbuild_dir="out"\n'
                   '[[target]]\nname="parts"\ntype="static"\nsources=["parts/*.{c,cpp}"]\n'
                   '[[target]]\nname="stressapp"\nsources=["main.cpp"]\nlinks=["parts"]\n'
                   '[[test]]\nname="check"\nsources=["main.cpp"]\nlinks=["parts"]\n')
        for jobs in (1, 4):
            with self.subTest(jobs=jobs):
                self.configure()
                self.assertIn(str(expected), self.cli('run', '--jobs', str(jobs)))
                self.assertIn('1 passed', self.cli('test', '--jobs', str(jobs)))
                build = self.app / 'out'
                artifacts = [path for path in build.rglob('*')
                             if path.is_file() and path.suffix in ('.o', '.obj', '.pch', '.gch')]
                self.assertTrue(any(path.suffix in ('.pch', '.gch') for path in artifacts))
                self.assertGreaterEqual(sum(path.suffix in ('.o', '.obj') for path in artifacts), count + 2)
                stamps = {path: path.stat().st_mtime_ns for path in artifacts}
                for _ in range(OPTIONS.repeats):
                    self.cli('build', '--jobs', str(jobs))
                    self.cli('test', '--jobs', str(jobs))
                    self.assertEqual(stamps, {path: path.stat().st_mtime_ns for path in artifacts},
                                     'unchanged parallel build recompiled a unit or PCH')
                self.cli('clean')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + field, required=True)
    parser.add_argument('--packages', type=bounded_count, default=DEFAULT_PACKAGES)
    parser.add_argument('--units', type=bounded_count, default=DEFAULT_UNITS)
    parser.add_argument('--repeats', type=bounded_count, default=DEFAULT_REPEATS)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
