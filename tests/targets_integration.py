#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build real target DAGs with native generators; Python is a test dependency only."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


# Fixture policy: each side of the diamond returns the base value.
BASE_API_VALUE = 7
DIAMOND_BRANCH_COUNT = 2
SHARED_API_VALUE = BASE_API_VALUE * DIAMOND_BRANCH_COUNT
# Keep the sentinel shared by fixture creation and archive freshness assertions.
REMOVED_SYMBOL = 'CLUTCH_REMOVED_SYMBOL_UNIQUE'


class TargetTests(unittest.TestCase):
    """Exercise selection, link interfaces, runtime artifacts and archive freshness."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch targets ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        for folder in ('base', 'shared', 'apps', 'checks'):
            (self.root / folder).mkdir()
        self.write('base/base.h', '#pragma once\n#ifdef __cplusplus\nextern "C" {\n#endif\n'
                   'int base(void);\n#ifdef __cplusplus\n}\n#endif\n')
        self.write('base/base.cpp', '#include "base.h"\n#include <vector>\n'
                   '#ifndef BASE_PRIVATE\n#error missing private definition\n#endif\n'
                   'int base(void) { return std::vector<int>{BASE_API}.front(); }\n')
        self.write('base/stale.cpp', f'int {REMOVED_SYMBOL}() {{ return 0; }}\n')
        for name in ('left', 'right'):
            self.write(f'base/{name}.cpp', '#include "base.h"\n'
                       '#ifdef BASE_PRIVATE\n#error private definition escaped\n#endif\n'
                       f'int {name}() {{ return base(); }}\n')
        self.write('shared/api.h', '#pragma once\n#ifdef _WIN32\n'
                   '#ifdef SHARED_BUILD\n#define API __declspec(dllexport)\n#else\n'
                   '#define API __declspec(dllimport)\n#endif\n#else\n#define API\n#endif\n'
                   '#ifdef __cplusplus\nextern "C" {\n#endif\nAPI int answer(void);\n'
                   '#ifdef __cplusplus\n}\n#endif\n')
        self.write('shared/api.cpp', '#include "api.h"\nint left(); int right();\n'
                   '#ifndef BASE_API\n#error missing transitive public definition\n#endif\n'
                   'int answer(void) { return left() + right(); }\n')
        main = '#include "api.h"\n#ifdef BASE_API\n#error private link interface escaped\n#endif\n'
        main += 'int main(void) { return answer() == SHARED_API ? 0 : 1; }\n'
        self.write('apps/main.c', main)
        self.write('checks/check.cpp', main)
        self.write('apps/optional.cpp', 'int main() { return 0; }\n')
        self.targets = f'''
[[target]]
name = "base"
type = "static"
sources = ["base/base.cpp", "base/stale.cpp"]
public_include_dirs = ["base"]
public_defines = ["BASE_API={BASE_API_VALUE}"]
defines = ["BASE_PRIVATE"]
[[target]]
name = "left"
type = "static"
sources = ["base/left.cpp"]
public_links = ["base"]
[[target]]
name = "right"
type = "static"
sources = ["base/right.cpp"]
links = ["base"]
[[target]]
name = "shared"
type = "shared"
sources = ["shared/api.cpp"]
links = ["left", "right"]
public_include_dirs = ["shared"]
public_defines = ["SHARED_API={SHARED_API_VALUE}"]
defines = ["SHARED_BUILD"]
[[target]]
name = "app"
sources = ["apps/main.c"]
links = ["shared"]
[[target]]
name = "optional"
sources = ["apps/optional.cpp"]
default = false
[[test]]
name = "check"
sources = ["checks/check.cpp"]
links = ["shared"]
'''

    def write(self, relative, text):
        """Write portable UTF-8 fixture inputs with deterministic newlines."""
        (self.root / relative).write_text(text, encoding='utf-8')

    def manifest(self, generator='ninja', targets=None, build_dir='target build'):
        """Choose a fixture profile path; readers discover it through locate."""
        self.write('clutch.toml', '[project]\nname="target_probe"\nversion="0.1.0"\n'
                   'language="mixed"\nstd={c=11,cpp=17}\n'
                   f'[build]\ngenerator="{generator}"\n'
                   f'[profile.debug]\nbuild_dir={json.dumps(build_dir)}\n' +
                   (self.targets if targets is None else targets))

    def cli(self, *arguments, success=True):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def configure(self, generator='ninja'):
        """Request target artifacts without guessing directories or file names."""
        location = json.loads(self.cli('locate').stdout)
        query = Path(location['paths']['query']) / 'codemodel-v1'
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()
        args = ['.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx]
        if generator == 'ninja':
            args += ['--build-tool', OPTIONS.ninja]
        result = self.cli(*args)
        reply = Path(location['paths']['reply'])
        index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text(encoding='utf-8'))
        reference = next(item for item in index['objects'] if item['kind'] == 'codemodel')
        model = json.loads((reply / reference['jsonFile']).read_text(encoding='utf-8'))
        self.configured_targets = {
            ref['name']: json.loads((reply / ref['jsonFile']).read_text(encoding='utf-8'))
            for ref in model['targets']}
        return result

    def build_path(self, name):
        """The published run action identifies an executable on any platform."""
        return Path(self.configured_targets[name]['actions']['run']['arguments'][0])

    def artifacts(self, name):
        """Follow the artifact contract, including compiler-specific import names."""
        return [Path(item['path']) for item in self.configured_targets[name]['artifacts']]

    def generators(self):
        result = ['ninja']
        if os.name == 'nt':
            result.extend(name for name, tool in [('nmake', 'nmake')]
                          if shutil.which(tool))
            # Native Visual Studio projects require cl, while NMake also accepts
            # clang-cl. Do not equate the two compiler families here.
            if all(Path(compiler).stem.lower() == 'cl' for compiler in (OPTIONS.cc, OPTIONS.cxx)):
                result.append('visual-studio')
        else:
            if shutil.which('gmake') or shutil.which('make'):
                result.append('make')
            if sys.platform == 'darwin' and shutil.which('xcodebuild'):
                result.append('xcode')
        return result

    def test_native_generators(self):
        """The same DAG builds, runs, stages DLLs and stays incremental everywhere."""
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator)
                self.configure(generator)
                self.cli('build', '--target', 'app')
                self.assertFalse(self.build_path('optional').exists())
                self.cli('run', '--target', 'app')
                self.cli('test', '--target', 'check')
                app = self.build_path('app')
                stamp = app.stat().st_mtime_ns
                self.cli('build', '--target', 'app')
                self.assertEqual(stamp, app.stat().st_mtime_ns, 'no-op build relinked')
                self.cli('build')
                self.assertFalse(self.build_path('optional').exists())
                self.cli('build', '-t', 'optional')
                self.assertTrue(self.build_path('optional').exists())
                self.cli('run', '-t', 'base', success=False)
                self.cli('build', '-t', 'missing', success=False)
                self.cli('test', '-t', 'app', success=False)

                # A changed DLL must refresh the staged copy even if its test exe
                # is already linked. POSIX consumers exercise relative rpaths.
                self.write('shared/api.cpp', (self.root / 'shared/api.cpp').read_text() + '\n// changed\n')
                self.cli('test')
                if os.name == 'nt':
                    shared_artifacts = self.artifacts('shared')
                    source = next(path for path in shared_artifacts if path.suffix.lower() == '.dll')
                    staged = self.build_path('check').parent / source.name
                    self.assertEqual(source.read_bytes(), staged.read_bytes())
                    imports = [path for path in shared_artifacts if path != source]
                    self.assertEqual(len(imports), 1, shared_artifacts)
                    import_library, = imports
                    import_library.unlink()
                    self.cli('run', '--target', 'app')
                    self.assertTrue(import_library.exists())

                # Recreating an archive drops members for removed source files.
                archives = self.artifacts('base')
                self.assertEqual(len(archives), 1, archives)
                archive, = archives
                sentinel = REMOVED_SYMBOL.encode('ascii')
                self.assertIn(sentinel, archive.read_bytes())
                self.manifest(generator, self.targets.replace(', "base/stale.cpp"', ''))
                self.configure(generator)
                self.cli('build', '-t', 'base')
                self.assertNotIn(sentinel, archive.read_bytes())
                self.cli('clean')

    def test_file_api_and_saved_targets(self):
        """Public metadata exposes concrete kinds, requirements and dependency IDs."""
        self.manifest()
        self.configure()
        targets = self.configured_targets
        self.assertEqual(targets['base']['type'], 'STATIC_LIBRARY')
        self.assertEqual(targets['shared']['type'], 'SHARED_LIBRARY')
        self.assertEqual(targets['app']['type'], 'EXECUTABLE')
        self.assertNotIn('run', targets['base']['actions'])
        self.assertTrue(targets['check']['isTest'])
        self.assertFalse(targets['optional']['buildByDefault'])
        self.assertEqual(targets['app']['dependencies'],
                         [targets['shared']['id']])
        listing = self.cli('targets').stdout
        self.assertIn('base  static', listing)
        self.assertIn('check  executable (test)', listing)
        self.cli('test')  # Restores schema-4 configuration in a separate process.

    def test_external_profile_directory(self):
        """Target checks also work when the configured output is outside sources."""
        with tempfile.TemporaryDirectory(prefix='clutch target outputs ') as temporary:
            output = Path(temporary).resolve() / 'external debug'
            self.manifest(build_dir=output.as_posix())
            self.configure()
            self.cli('run', '--target', 'app')
            self.cli('test', '--target', 'check')
            self.assertTrue(self.build_path('app').is_relative_to(output))
            self.assertTrue(self.build_path('check').is_relative_to(output))
            self.cli('clean')
            self.assertFalse(output.exists())

    def test_invalid_graphs(self):
        """Reject unknown targets, executable links, cycles and ambiguous source modes."""
        invalid = [self.targets.replace('links = ["shared"]', 'links = ["missing"]'),
                   self.targets.replace('links = ["shared"]', 'links = ["optional"]'),
                   self.targets.replace('defines = ["BASE_PRIVATE"]', 'links = ["left"]'),
                   self.targets.replace('name = "optional"', 'name = "app"'),
                   self.targets.replace('type = "static"', 'type = "unknown"'),
                   self.targets.replace('default = false', 'default = "false"')]
        for targets in invalid:
            self.manifest(targets=targets)
            self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                     '--build-tool', OPTIONS.ninja, success=False)
        for name in (('NUL', 'COM1') if os.name == 'nt' else ('obj', 'Makefile')):
            self.manifest(targets=self.targets.replace('name = "optional"', f'name = "{name}"'))
            self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                     '--build-tool', OPTIONS.ninja, success=False)

    def test_library_only_and_multiple_applications(self):
        """Library-only projects need no main; ambiguous run requires selection."""
        library = self.targets.split('[[target]]\nname = "left"')[0]
        self.manifest(targets=library)
        self.configure()
        self.cli('build')
        self.cli('run', success=False)
        self.cli('clean')
        apps = '''[[target]]
name="first"
sources=["apps/optional.cpp"]
[[target]]
name="second"
sources=["apps/optional.cpp"]
'''
        self.manifest(targets=apps)
        self.configure()
        self.cli('build')
        self.assertTrue(self.build_path('first').exists())
        self.assertTrue(self.build_path('second').exists())
        self.cli('run', success=False)
        self.cli('run', '-t', 'second')

    def test_c_executable_with_cpp_archive(self):
        """A C consumer inherits headers/macros and gets a C++ runtime at final link."""
        self.write('apps/direct.c', '#include "base.h"\n'
                   'int main(void) { return base() == BASE_API ? 0 : 1; }\n')
        library = self.targets.split('[[target]]\nname = "left"')[0]
        self.manifest(targets=library + '''
[[target]]
name="direct"
sources=["apps/direct.c"]
links=["base"]
''')
        self.configure()
        self.cli('run', '-t', 'direct')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *arguments])
