#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise native template generation, Git identity and direct executor refresh."""

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

ROOT = Path(__file__).resolve().parents[2]
COMMAND_TIMEOUT = 120


class ConfiguredFileTests(unittest.TestCase):
    """Keep template ownership, build rules and language-server metadata aligned."""

    def setUp(self):
        work = None
        if os.name == 'nt':
            work = ROOT / 'tests/work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='configured files ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.write('VERSION', '1.2.3\n')
        self.write('main.cpp', '#include <build_info.hpp>\n#include <cstdio>\n'
                   'int helper(); int main() { std::puts(identity); return helper(); }\n')
        self.write('helper.cpp', 'int helper() { return 0; }\n')
        self.write('build_info.in', '/**\n * @file\n * @brief Native metadata.\n */\n'
                   '#pragma once\ninline constexpr auto identity = "@PROJECT_VERSION@";\n'
                   '// @PROJECT_NAME@ @VALUE@ @@literal@@\n')
        self.manifest()

    def write(self, name, text):
        """Write bounded, portable fixture inputs with deterministic newlines."""
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def cli(self, *arguments, success=True, env=None):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                capture_output=True, text=True, timeout=COMMAND_TIMEOUT, env=env)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def manifest(self, generator='ninja', git=False, extra=''):
        self.build = self.root / ('out-' + generator)
        self.write('clutch.toml', '[project]\nname="metadata"\nversion_file="VERSION"\n'
                   'language="cpp"\nstd=17\n[build]\nsources=["main.cpp","helper.cpp"]\n'
                   f'generator={json.dumps(generator)}\n'
                   f'[profile.debug]\nbuild_dir={json.dumps(self.build.name)}\n'
                   '[[configure_file]]\ninput="build_info.in"\noutput="build_info.hpp"\n'
                   f'git_version={str(git).lower()}\nvariables={{VALUE="literal @PROJECT_NAME@"}}\n' + extra)

    def configure(self, success=True):
        return self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        '--build-tool', OPTIONS.ninja if self.build.name == 'out-ninja'
                        else self.executor(), success=success)

    def header(self):
        """Find the declared output without depending on its namespace hash."""
        return next(self.build.glob('generated/*/build_info.hpp'))

    def executor(self):
        family = self.build.name.removeprefix('out-')
        return {'ninja': OPTIONS.ninja,
                'make': shutil.which('gmake') or shutil.which('make'),
                'nmake': shutil.which('nmake'), 'jom': shutil.which('jom'),
                'visual-studio': shutil.which('MSBuild') or shutil.which('msbuild'),
                'xcode': shutil.which('xcodebuild')}[family]

    def generators(self):
        yield 'ninja'
        if os.name == 'nt':
            if shutil.which('nmake'):
                yield 'nmake'
            if Path(OPTIONS.cxx).stem.lower() in ('cl', 'clang-cl') and shutil.which('MSBuild'):
                yield 'visual-studio'
        else:
            if shutil.which('gmake') or shutil.which('make'):
                yield 'make'
            if sys.platform == 'darwin':
                yield 'xcode'

    def direct(self):
        """Invoke the generated native rules without going through clutch build."""
        family = self.build.name.removeprefix('out-')
        command = [self.executor()]
        if family == 'ninja':
            command += ['-C', str(self.build)]
        elif family == 'visual-studio':
            command += ['clutch.proj', '/nologo', '/verbosity:minimal']
        elif family == 'xcode':
            command += ['-project', 'metadata.xcodeproj', '-configuration', 'Debug', '-target', 'metadata']
        result = subprocess.run(command, cwd=self.build, capture_output=True, text=True,
                                timeout=COMMAND_TIMEOUT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_outputs_direct_refresh_and_noop(self):
        """Headers are regenerated before compilation and unchanged builds stay clean."""
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator)
                location = json.loads(self.cli('locate').stdout)
                query = Path(location['paths']['query'])
                query.mkdir(parents=True, exist_ok=True)
                (query / 'codemodel-v1').touch()
                (query / 'inputs-v1').touch()
                self.configure()
                header = self.header()
                text = header.read_text(encoding='utf-8')
                self.assertIn('@file', text)
                self.assertIn('metadata literal @PROJECT_NAME@ @literal@', text)
                database = json.loads((self.build / 'compile_commands.json').read_text())
                self.assertEqual(len(database), 2)  # Template outputs are not units.
                self.assertTrue(all(any(str(header.parent) in arg for arg in item['arguments'])
                                    for item in database))
                reply = Path(location['paths']['reply'])
                index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text(encoding='utf-8'))
                references = {item['kind']: item for item in index['objects']}
                inputs = json.loads((reply / references['inputs']['jsonFile']).read_text(encoding='utf-8'))
                self.assertEqual(Path(inputs['sourceDiscovery']['directory']), self.root)
                self.assertIn('.hpp', inputs['sourceDiscovery']['extensions'])
                self.assertIn(self.root / 'VERSION',
                              {Path(item['path']) for item in inputs['configurationInputs']})
                model = json.loads((reply / references['codemodel']['jsonFile']).read_text(encoding='utf-8'))
                target = json.loads((reply / model['targets'][0]['jsonFile']).read_text(encoding='utf-8'))
                self.assertIn(header, set(map(Path, target['additionalFiles'])))
                self.direct()
                snapshots = {Path(item['output']): Path(item['output']).stat().st_mtime_ns
                             for item in database}
                header_stamp = header.stat().st_mtime_ns
                self.direct()
                self.assertEqual(header.stat().st_mtime_ns, header_stamp)
                self.assertEqual(snapshots, {path: path.stat().st_mtime_ns for path in snapshots})
                header.unlink()
                self.direct()
                self.assertEqual(header.read_text(encoding='utf-8'), text)
                self.assertIn('1.2.3', self.cli('run').stdout)

    def test_version_input_changes_require_configuration(self):
        """Version files are fingerprinted like manifests rather than silently stale."""
        self.configure()
        self.write('VERSION', '2.0.0\n')
        self.assertIn('configuration input changed', self.cli('build', success=False).stderr)
        self.configure()
        self.assertIn('2.0.0', self.cli('run').stdout)

    def test_workspace_template_namespaces_are_private(self):
        """Equal output names in separate members do not leak compile interfaces."""
        self.write('clutch.toml', '[workspace]\nmembers=["left","right","app"]\n'
                   '[build]\ngenerator="ninja"\n[profile.debug]\nbuild_dir="out-ninja"\n')
        for name, value in (('left', 7), ('right', 9)):
            self.write(f'{name}/VERSION', f'{value}.0.0\n')
            self.write(f'{name}/value.in', '#pragma once\n#define MEMBER_VALUE @VALUE@\n')
            self.write(f'{name}/library.cpp', '#include <same.hpp>\n'
                       f'int {name}() {{ return MEMBER_VALUE; }}\n')
            self.write(f'{name}/clutch.toml', '[project]\n'
                       f'name="{name}"\nversion_file="VERSION"\n'
                       '[[target]]\n' + f'name="{name}"\ntype="static"\nsources=["library.cpp"]\n'
                       '[[configure_file]]\ninput="value.in"\noutput="same.hpp"\n'
                       f'variables={{VALUE="{value}"}}\n')
        self.write('app/main.cpp', 'int left(); int right();\n'
                   'int main() { return left()==7 && right()==9 ? 0 : 1; }\n')
        self.write('app/clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n'
                   '[[target]]\nname="app"\nsources=["main.cpp"]\n'
                   'links=["left::left","right::right"]\n')
        self.configure()
        self.cli('run', '--target', 'app::app')
        self.assertEqual(len(list(self.build.glob('generated/*/same.hpp'))), 2)
        self.write('left/VERSION', '8.0.0\n')
        self.assertIn('configuration input changed', self.cli('build', success=False).stderr)

    def test_native_package_version_files_and_templates(self):
        """Package constraints use version files, and generated headers stay private."""
        self.write('library/VERSION', '9.0.0\n')
        self.write('library/value.in', '#pragma once\n'
                   'namespace library_info {\n'
                   'inline constexpr auto identity="@PROJECT_VERSION@";\n}\n')
        self.write('library/library.cpp', '#include <build_info.hpp>\n'
                   'int local_number() { return library_info::identity[0]-\'0\'; }\n')
        self.write('library/clutch.toml', '[project]\nname="number"\nversion_file="VERSION"\n'
                   '[[target]]\nname="number"\ntype="static"\nsources=["library.cpp"]\n'
                   '[[configure_file]]\ninput="value.in"\noutput="build_info.hpp"\n')
        self.write('helper.cpp', 'int local_number();\n'
                   'int helper() { return local_number()==9 ? 0 : 1; }\n')
        self.manifest(extra='[dependencies]\nnumber={path="library",target="number",version="^9.0.0"}\n')
        self.configure()
        self.assertIn('1.2.3', self.cli('run').stdout)
        self.assertEqual(len(list(self.build.glob('generated/*/build_info.hpp'))), 2)
        self.write('library/VERSION', '8.0.0\n')
        self.assertIn('configuration input changed', self.cli('build', success=False).stderr)
        self.assertIn('version conflict', self.configure(success=False).stderr)

    def test_templates_and_version_inputs_relocate_in_source_bundles(self):
        """Snapshots preserve declarations and require the inputs they regenerate."""
        self.configure()
        archive = self.root / 'snapshot.zip'
        self.cli('bundle', '--output', str(archive))
        relocated = self.root / 'relocated'
        with zipfile.ZipFile(archive) as snapshot:
            snapshot.extractall(relocated)
            self.assertIn('VERSION', snapshot.namelist())
            self.assertIn('build_info.in', snapshot.namelist())
        for args in (['.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                      '--build-tool', OPTIONS.ninja], ['run']):
            result = subprocess.run([OPTIONS.clutch, *args], cwd=relocated,
                                    capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        failure = self.cli('bundle', '--output', str(self.root / 'missing-input.zip'),
                           '--exclude', 'VERSION', success=False)
        self.assertIn('required', failure.stderr)

    def test_bad_template_does_not_publish_partial_outputs(self):
        """A later malformed output cannot overwrite an earlier valid file."""
        location = json.loads(self.cli('locate').stdout)
        query = Path(location['paths']['query'])
        query.mkdir(parents=True, exist_ok=True)
        (query / 'codemodel-v1').touch()
        self.configure()
        original = self.header().read_bytes()
        self.write('bad.in', '@MISSING_VARIABLE@')
        self.manifest(extra='[[configure_file]]\ninput="bad.in"\noutput="bad.hpp"\n')
        self.assertIn('unknown template variable', self.configure(success=False).stderr)
        self.assertEqual(self.header().read_bytes(), original)
        self.assertFalse(list(self.build.glob('generated/*/bad.hpp')))
        reply = Path(location['paths']['reply'])
        index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text(encoding='utf-8'))
        self.assertEqual(index['status'], 'error')
        self.assertEqual(index['error']['code'], 'configure-failed')

    def test_paths_schema_and_version_are_strict(self):
        """Reject path escapes, duplicate outputs, malformed fields and bad versions."""
        for output in ('../outside.hpp', '/absolute.hpp', '.', '', 'folder/'):
            with self.subTest(output=output):
                self.manifest()
                text = (self.root / 'clutch.toml').read_text().replace(
                    'output="build_info.hpp"', 'output=' + json.dumps(output))
                self.write('clutch.toml', text)
                self.configure(success=False)
        for extra, diagnostic in (
                ('[[configure_file]]\ninput="build_info.in"\noutput="build_info.hpp"\n', 'duplicate'),
                ('[[configure_file]]\ninput="build_info.in"\noutput="build_info.hpp/nested"\n', 'conflicting'),
                ('[[configure_file]]\ninput="build_info.in"\noutput="other.hpp"\n'
                 'variables={PROJECT_NAME="override"}\n', 'reserved')):
            self.manifest(extra=extra)
            self.assertIn(diagnostic, self.configure(success=False).stderr)
        for version in ('01.2.3', 'v1.2.3', '1.2', '1.2.3+metadata', ''):
            self.manifest()
            self.write('VERSION', version)
            self.assertIn('canonical', self.configure(success=False).stderr)

    def test_size_limits_and_serialized_callbacks(self):
        """Bound template reads and serialize concurrent idempotent native actions."""
        self.configure()
        before = self.header().read_bytes()
        with ThreadPoolExecutor(max_workers=4) as workers:
            futures = [workers.submit(self.cli, '__configure-files') for _ in range(8)]
            for future in futures:
                future.result()
        self.assertEqual(self.header().read_bytes(), before)

        self.write('build_info.in', 'x' * (1024 * 1024 + 1))
        self.assertIn('size limit', self.configure(success=False).stderr)
        self.assertEqual(self.header().read_bytes(), before)

    @unittest.skipUnless(shutil.which('git'), 'Git is optional')
    def test_worktrees_shallow_repositories_and_no_git(self):
        """Resolve only this project's Git boundary, and keep Git optional."""
        self.manifest(git=True)
        self.configure()
        self.write('.gitignore', '/out-*/\n/.clutch/\n')
        def git(*arguments):
            return subprocess.check_output(['git', '-C', str(self.root), *arguments], text=True).strip()
        git('init', '-q')
        git('config', 'user.name', 'Clutch metadata test')
        git('config', 'user.email', 'test@example.invalid')
        git('config', 'commit.gpgsign', 'false')
        git('config', 'core.autocrlf', 'false')
        git('add', '.')
        git('commit', '-qm', 'metadata fixture')
        original = self.root
        extra = tempfile.TemporaryDirectory(prefix='metadata git ')
        self.addCleanup(extra.cleanup)
        worktree = Path(extra.name) / 'worktree'
        git('worktree', 'add', '-q', '--detach', str(worktree))
        self.root = worktree
        self.build = worktree / 'out-ninja'
        self.configure()
        self.assertIn('-dev+g', self.header().read_text())
        self.assertNotIn('+source', self.header().read_text())
        shallow = Path(extra.name) / 'shallow'
        subprocess.run(['git', 'clone', '-q', '--depth', '1', original.as_uri(), str(shallow)],
                       check=True, capture_output=True, timeout=COMMAND_TIMEOUT)
        self.root = shallow
        self.build = shallow / 'out-ninja'
        self.configure()
        self.assertIn('.shallow', self.header().read_text())
        env = os.environ.copy()
        env['PATH'] = str(Path(extra.name) / 'no-tools')
        self.cli('__configure-files', env=env)
        self.assertIn('1.2.3+source', self.header().read_text())

    @unittest.skipUnless(shutil.which('git'), 'Git is optional')
    def test_git_identity_parity_and_native_refresh(self):
        """Compare native identity with the established bootstrap policy in local repos."""
        self.manifest(git=True)
        self.configure()
        self.assertIn('1.2.3+source', self.header().read_text())
        self.write('.gitignore', '/out-*/\n/.clutch/\n')
        source_template = ROOT / 'src/core/version.hpp.in'
        self.write('src/core/version.hpp.in', source_template.read_text(encoding='utf-8'))
        def git(*arguments):
            return subprocess.check_output(['git', '-C', str(self.root), *arguments], text=True).strip()
        git('init', '-q')
        git('config', 'user.name', 'Clutch metadata test')
        git('config', 'user.email', 'test@example.invalid')
        git('config', 'commit.gpgsign', 'false')
        git('config', 'tag.gpgsign', 'false')
        git('config', 'core.autocrlf', 'false')
        git('add', '.')
        git('commit', '-qm', 'metadata fixture')
        reference_temporary = tempfile.TemporaryDirectory(prefix='metadata reference ')
        self.addCleanup(reference_temporary.cleanup)
        reference = Path(reference_temporary.name)
        for state in ('development', 'release', 'dirty', 'clean'):
            with self.subTest(state=state):
                if state == 'release':
                    git('tag', 'v1.2.3')
                elif state == 'dirty':
                    self.write('new.txt', 'untracked change\n')
                elif state == 'clean':
                    (self.root / 'new.txt').unlink()
                self.cli('__configure-files')
                native = self.header().read_text(encoding='utf-8')
                if shutil.which('cmake'):
                    subprocess.run(['cmake', f'-DCLUTCH_SOURCE_DIR={self.root}',
                                    f'-DCLUTCH_BINARY_DIR={reference}', '-P',
                                    str(ROOT / 'cmake/GenerateVersion.cmake')], check=True,
                                   capture_output=True, text=True, timeout=COMMAND_TIMEOUT)
                    self.assertIn((reference / 'generated/version.txt').read_text().strip(), native)
                if state in ('release', 'clean'):
                    self.assertIn('"1.2.3"', native)
                elif state == 'dirty':
                    self.assertIn('.dirty', native)
                else:
                    self.assertIn('-dev+g', native)
        self.direct()
        objects = {Path(item['output']): Path(item['output']).stat().st_mtime_ns
                   for item in json.loads((self.build / 'compile_commands.json').read_text())}
        self.write('new.txt', 'native direct refresh\n')
        self.direct()
        self.assertIn('.dirty', self.header().read_text())
        self.assertTrue(any(path.stat().st_mtime_ns != stamp for path, stamp in objects.items()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        value = getattr(OPTIONS, option)
        setattr(OPTIONS, option, os.path.abspath(shutil.which(value) or value))
    unittest.main(argv=[sys.argv[0], *arguments])
