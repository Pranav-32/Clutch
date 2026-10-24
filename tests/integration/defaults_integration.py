#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Verify automatic native generators and manifest-relative source discovery."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


# Remove host overrides so availability tests describe their own installations.
TOOL_VARIABLES = ('CLUTCH_BUILD_TOOL', 'NINJA', 'MAKE', 'NMAKE', 'JOM',
                  'MSBUILD', 'XCODEBUILD')
COMMAND_TIMEOUT = 120


class DefaultTests(unittest.TestCase):
    """Use isolated projects and real tools, without changing the host setup."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch defaults ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.env = {key: value for key, value in os.environ.items()
                    if key.upper() not in TOOL_VARIABLES}
        self.env['NINJA'] = OPTIONS.ninja
        self.manifest()

    def write(self, name, text):
        """Create UTF-8 inputs relative to this manifest, including nested trees."""
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def manifest(self, language='cpp', extra='', entry='', build_dir=None):
        """Omit generator and sources unless a test explicitly supplies them."""
        self.write('clutch.toml', '[project]\nname="auto_app"\nversion="1.0.0"\n'
                   f'language={json.dumps(language)}\n' + entry + extra +
                   (f'[profile.debug]\nbuild_dir={json.dumps(build_dir)}\n'
                    if build_dir else ''))

    def cli(self, *arguments, success=True, env=None):
        """Preserve native diagnostics and bound subprocess lifetime."""
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                env=self.env if env is None else env,
                                text=True, capture_output=True, timeout=COMMAND_TIMEOUT)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def configure(self, *arguments, env=None, success=True):
        """Choose only compilers; generator/executor defaults remain under test."""
        return self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        *arguments, env=env, success=success)

    def model(self):
        """Consume published IDE files instead of parsing internal TOML state."""
        location = json.loads(self.cli('locate').stdout)
        reply = Path(location['paths']['reply'])
        index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text(encoding='utf-8'))
        reference = next(item for item in index['objects'] if item['kind'] == 'codemodel')
        model = json.loads((reply / reference['jsonFile']).read_text(encoding='utf-8'))
        targets = {item['name']: json.loads((reply / item['jsonFile']).read_text(encoding='utf-8'))
                   for item in model['targets']}
        database = json.loads(Path(location['paths']['compilationDatabase']).read_text(encoding='utf-8'))
        return targets, database

    def query(self):
        """Register a codemodel consumer before the configuration pass."""
        location = json.loads(self.cli('locate').stdout)
        query = Path(location['paths']['query']) / 'codemodel-v1'
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()

    def isolated_environment(self):
        """Hide optional IDE bundles and PATH tools without uninstalling anything."""
        # Windows initializes the native ProgramFiles value from ProgramW6432
        # when creating a 64-bit child, so both must describe the fixture.
        hidden = (*TOOL_VARIABLES, 'PATH', 'PROGRAMFILES', 'PROGRAMFILES(X86)',
                  'PROGRAMW6432', 'VSINSTALLDIR')
        env = {key: value for key, value in self.env.items()
               if key.upper() not in hidden}
        empty = self.root / 'empty tools'
        empty.mkdir(exist_ok=True)
        env.update(PATH=str(empty), PROGRAMFILES=str(empty), PROGRAMW6432=str(empty),
                   **{'PROGRAMFILES(X86)': str(empty), 'VSINSTALLDIR': str(empty)})
        return env

    def test_implicit_languages_headers_and_project_boundaries(self):
        """Language filtering precedes validation; headers never become units."""
        for language in ('c', 'cpp', 'c++', 'mixed', 'auto'):
            with self.subTest(language=language):
                self.manifest(language, build_dir=f'output/{language}')
                if language == 'c':
                    self.write('main.c', 'int helper(void); int main(void) { return helper(); }\n')
                    self.write('main.cpp', '#error unrelated C++ source\n')
                else:
                    self.write('main.cpp', 'int helper(); int main() { return helper(); }\n')
                    self.write('main.c', '#error unrelated C source\n' if language in ('cpp', 'c++')
                               else 'int c_helper(void) { return 0; }\n')
                extension = 'c' if language == 'c' else 'cpp'
                other = 'cpp' if language == 'c' else 'c'
                stale = self.root / f'modules/helper.{other}'
                if stale.exists():
                    stale.unlink()
                self.write(f'modules/helper.{extension}', 'int helper(void) { return 0; }\n')
                for name in ('common.h', 'shared.H', 'modules/detail.hpp'):
                    self.write(name, '#error headers must not be compiled\n')
                self.write('member/clutch.toml', '[project]\nname="other"\nversion="1.0.0"\n')
                self.write('member/broken.cpp', '#error separate manifest\n')
                self.write('.clutch/cache/broken.c', '#error cached dependency\n')
                self.write('old build/CMakeCache.txt', '')
                self.write('old build/broken.cpp', '#error generated\n')
                self.query()
                self.configure()
                targets, database = self.model()
                expected = {'main.' + extension, 'modules/helper.' + extension}
                if language in ('mixed', 'auto'):
                    expected.add('main.c')
                self.assertEqual({Path(item['file']).relative_to(self.root).as_posix()
                                  for item in database}, expected)
                headers = {'common.h', 'shared.H'}
                if language != 'c':
                    headers.add('modules/detail.hpp')
                self.assertEqual({Path(path).relative_to(self.root).as_posix()
                                  for path in targets['auto_app']['additionalFiles']}, headers)
                self.cli('run')

    def test_local_and_recursive_patterns_are_manifest_relative(self):
        """Single stars stay local, double stars include descendant files."""
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.write('nested/extra.cpp', 'int extra() { return 0; }\n')
        self.write('nested/value.hpp', '#pragma once\n')
        selections = (('*.cpp', 1), ('**.cpp', 2), ('**/*.cpp', 2),
                      ('*.{cpp,hpp}', 1), ('**.{cpp,hpp}', 2))
        for index, (pattern, count) in enumerate(selections):
            with self.subTest(pattern=pattern):
                # Each selection owns a product path: this tests discovery, not
                # rapid Windows executable replacement after a previous run.
                self.manifest(extra=f'[build]\nsources=[{json.dumps(pattern)}]\n',
                              build_dir=f'output/pattern-{index}')
                self.query()
                self.configure()
                targets, database = self.model()
                self.assertEqual(len(database), count)
                self.assertEqual(len(targets['auto_app']['additionalFiles']),
                                 1 if pattern == '**.{cpp,hpp}' else 0)
                self.cli('run')

    def test_explicit_entry_and_sources_disable_discovery(self):
        """Explicit inputs do not accidentally compile neighboring programs."""
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.write('nested/broken.cpp', '#error not selected\n')
        for name, entry, extra in (('entry', 'entry="main.cpp"\n', ''),
                                   ('sources', '', '[build]\nsources=["main.cpp"]\n')):
            self.manifest(entry=entry, extra=extra, build_dir=f'output/{name}')
            self.configure()
            self.cli('run')

    def test_declared_tests_are_not_implicit_application_sources(self):
        """A manifest can discover its application while naming a separate test."""
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.write('checks/check.cpp', 'int main() { return 0; }\n')
        self.manifest(extra='[[test]]\nname="check"\nsources=["checks/check.cpp"]\n')
        self.configure()
        self.cli('run')
        self.cli('test')

    def test_workspace_members_discover_from_their_own_manifests(self):
        """The real root and each member retain independent language boundaries."""
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.write('member/main.c', 'int main(void) { return 0; }\n')
        self.write('member/clutch.toml', '[project]\nname="child"\nversion="1.0.0"\n'
                   'language="c"\n')
        self.manifest(extra='[workspace]\nmembers=["member"]\n')
        self.query()
        self.configure()
        targets, database = self.model()
        self.assertEqual(len(database), 2)
        self.assertEqual(len(targets['auto_app::auto_app']['sources']), 1)
        self.assertEqual(len(targets['child::child']['sources']), 1)
        self.cli('run', '--target', 'auto_app::auto_app')
        self.cli('run', '--target', 'child::child')

    def test_empty_and_explicit_wrong_language_inputs_are_errors(self):
        """Discovery does not turn headers into programs or filter explicit inputs."""
        self.write('only.h', '#pragma once\n')
        self.assertIn('no sources', self.configure(success=False).stderr)
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.manifest('c', extra='[build]\nsources=["main.cpp"]\n')
        self.assertIn('does not match', self.configure(success=False).stderr)

    def test_auto_ninja_and_explicit_tool_inference(self):
        """A standard executor name identifies its output format when omitted."""
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.configure()
        self.assertTrue((self.root / 'Clutch-build-debug/build.ninja').is_file())
        self.configure('--build-tool', OPTIONS.ninja)
        self.cli('run')
        failed = self.configure('--generator', 'ninja', '--build-tool',
                                str(self.root / 'missing-ninja'), success=False)
        self.assertIn('cannot find build generator executable', failed.stderr)
        self.assertIn('specify --generator', self.configure(
            '--build-tool', str(self.root / 'custom-wrapper'), success=False).stderr)

    def test_fallback_and_cache_keep_an_available_executor(self):
        """Without Ninja, select another installed generator and retain it."""
        family = 'nmake' if os.name == 'nt' else 'make'
        executor = shutil.which('nmake') if os.name == 'nt' else (
            shutil.which('gmake') or shutil.which('make'))
        if not executor:
            self.skipTest(f'{family} is not installed')
        self.write('main.cpp', 'int main() { return 0; }\n')
        env = self.isolated_environment()
        env[family.upper()] = executor
        self.configure(env=env)
        cache = self.root / 'Clutch-build-debug/backend.toml'
        self.assertIn(family, cache.read_text(encoding='utf-8'))
        self.env[family.upper()] = executor
        self.configure()  # Ninja is now available; the existing selection wins.
        self.assertIn(family, cache.read_text(encoding='utf-8'))
        self.cli('run')

    def test_no_executor_reports_candidates_without_publishing(self):
        """Unavailable tools produce a diagnostic instead of unusable build rules."""
        self.write('main.cpp', 'int main() { return 0; }\n')
        failure = self.configure(env=self.isolated_environment(), success=False)
        self.assertIn('no available compatible build generator', failure.stderr)
        self.assertFalse((self.root / 'Clutch-build-debug/configuration.toml').exists())
        explicit = self.configure('--generator', 'ninja', env=self.isolated_environment(),
                                  success=False)
        self.assertIn('selected build generator was not found: ninja', explicit.stderr)
        self.assertNotIn('no available compatible', explicit.stderr)

    @unittest.skipIf(os.name == 'nt', 'POSIX executable fixtures')
    def test_old_executors_do_not_block_compatible_fallback(self):
        """A found binary is insufficient if its version cannot read our output."""
        executor = shutil.which('gmake') or shutil.which('make')
        if not executor:
            self.skipTest('GNU Make is not installed')
        self.write('main.cpp', 'int main() { return 0; }\n')
        legacy = self.write('tools/ninja', '#!/bin/sh\necho 1.9.0\n')
        legacy.chmod(0o755)
        env = self.isolated_environment()
        env.update(NINJA=str(legacy), MAKE=executor)
        self.configure(env=env)
        self.assertIn('make', (self.root / 'Clutch-build-debug/backend.toml').read_text())
        failure = self.configure('--generator', 'ninja', '--build-tool', str(legacy),
                                 env=env, success=False)
        self.assertIn('Ninja 1.10', failure.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        value = getattr(OPTIONS, option)
        setattr(OPTIONS, option, str(Path(shutil.which(value) or value).resolve()))
    unittest.main(argv=[sys.argv[0], *arguments])
