#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build real target DAGs with native generators; Python is a test dependency only."""

import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


# Fixture policy: each side of the diamond returns the base value.
BASE_API_VALUE = 7
DIAMOND_BRANCH_COUNT = 2
SHARED_API_VALUE = BASE_API_VALUE * DIAMOND_BRANCH_COUNT
# Keep the sentinel shared by fixture creation and archive freshness assertions.
REMOVED_SYMBOL = 'CLUTCH_REMOVED_SYMBOL_UNIQUE'
# Advance a fixture prerequisite beyond whole-second backend timestamp precision.
NANOSECONDS_PER_SECOND = 1_000_000_000


class TargetTests(unittest.TestCase):
    """Exercise selection, link interfaces, runtime artifacts and archive freshness."""

    def setUp(self):
        # MSBuild excludes the system TEMP tree from header read tracking.
        # Keep Windows fixtures in the ignored workspace area, like SDK tests.
        work = None
        if os.name == 'nt':
            work = Path(__file__).resolve().parents[1] / 'work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch targets ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.create_fixture()

    def create_fixture(self):
        """Initialize independent source trees so failed subtests cannot leak state."""
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

    def configure(self, generator='ninja', release=False):
        """Request target artifacts without guessing directories or file names."""
        profile = ['--release'] if release else []
        location = json.loads(self.cli('locate', *profile).stdout)
        query = Path(location['paths']['query']) / 'codemodel-v1'
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()
        args = ['.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx]
        if generator == 'ninja':
            args += ['--build-tool', OPTIONS.ninja]
        result = self.cli(*args, *profile)
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

    def tie_make_timestamps(self, stamp):
        """Simulate one clock tick for response metadata, objects and final outputs."""
        paths = set()
        for target in self.configured_targets.values():
            paths.update(Path(item['path']) for item in target['artifacts'])
            paths.update(Path(source['object']) for source in target['sources'])
        location = json.loads(self.cli('locate').stdout)
        paths.update(Path(location['paths']['build']).rglob('*.make.rsp'))
        for path in paths:
            if path.is_file():
                os.utime(path, ns=(stamp, stamp))

    def coarse_clock_compiler(self, compiler, name):
        """Wrap a real POSIX compiler and emulate a clock shared with old archives."""
        wrapper = self.root / name
        # This test-only shim preserves the compiler's argv and exit status. Once
        # enabled, only newly compiled object mtimes are set to the fixture tick;
        # Make must still rebuild their consuming archives and linked products.
        wrapper.write_text(f'#!{sys.executable}\n'
                           'import os, pathlib, shlex, subprocess, sys\n'
                           f'compiler = {compiler!r}\n'
                           'result = subprocess.run([compiler, *sys.argv[1:]])\n'
                           'if result.returncode: sys.exit(result.returncode)\n'
                           'arguments = []\n'
                           'for argument in sys.argv[1:]:\n'
                           '    arguments.extend(shlex.split(pathlib.Path(argument[1:]).read_text())\n'
                           '                     if argument.startswith("@") else [argument])\n'
                           f'clock = pathlib.Path({str(self.root / "coarse-clock")!r})\n'
                           'if clock.exists() and "-c" in arguments and "-o" in arguments:\n'
                           '    stamp = int(clock.read_text())\n'
                           '    output = arguments[arguments.index("-o") + 1]\n'
                           '    os.utime(output, ns=(stamp, stamp))\n', encoding='utf-8')
        wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
        return str(wrapper)

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

    def test_run_forwards_flags_after_target_selection(self):
        """A repeatable --target must leave the application's -- separator intact."""
        self.write('apps/arguments.cpp', '#include <string>\n'
                   'int main(int argc, char** argv) {\n'
                   'return argc == 4 && std::string(argv[1]) == "--flag" &&\n'
                   'std::string(argv[2]) == "space value" &&\n'
                   'std::string(argv[3]) == "--" ? 17 : 1;\n}\n')
        self.manifest(targets='[[target]]\nname="arguments"\nsources=["apps/arguments.cpp"]\n')
        self.configure()
        result = self.cli('run', '--target', 'arguments', '--', '--flag',
                          'space value', '--', success=False)
        self.assertEqual(result.returncode, 17, result.stdout + result.stderr)

    def test_clean_build_recompiles_selected_dependencies_and_preserves_metadata(self):
        """Damage unchanged outputs so successful clean execution proves real work."""
        fixture_root = self.root
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.root = fixture_root / generator
                self.root.mkdir()
                self.create_fixture()
                self.write('prefix.h', '#pragma once\n#include <stddef.h>\n')
                self.manifest(generator)
                manifest = (self.root / 'clutch.toml').read_text()
                self.write('clutch.toml', manifest.replace('[build]',
                           '[build]\npch={c=["prefix.h"],cpp=["prefix.h"]}'))
                self.configure(generator)
                self.cli('build', '-t', 'app', '-t', 'check', '-t', 'optional', '--jobs', '2')
                location = json.loads(self.cli('locate').stdout)
                build = Path(location['paths']['build'])
                metadata = [build / 'configuration.toml', build / 'compile_commands.json',
                            self.root / 'clutch.lock']
                queries = Path(location['paths']['query'])
                metadata.extend(path for path in queries.rglob('*') if path.is_file())
                metadata.extend(path for path in Path(location['paths']['reply']).rglob('*') if path.is_file())
                snapshots = {path: path.read_bytes() for path in metadata}
                optional = self.build_path('optional')
                optional_stamp = optional.stat().st_mtime_ns
                optional_bytes = optional.read_bytes()
                operations = [('build', 'app'), ('run', 'app'), ('test', 'check')]
                if generator != 'ninja':
                    operations = [('run', 'app')]
                for command, target in operations:
                    with self.subTest(command=command):
                        executable = self.build_path(target)
                        stamp = executable.stat()
                        executable.write_bytes(bytes(stamp.st_size))
                        os.utime(executable, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
                        # Xcode owns its intermediate object locations; other
                        # generators use the exact graph paths exposed by the API.
                        object_file = None
                        if generator != 'xcode':
                            object_file = Path(self.configured_targets['base']['sources'][0]['object'])
                            object_stamp = object_file.stat()
                            object_file.write_bytes(bytes(object_stamp.st_size))
                            os.utime(object_file, ns=(object_stamp.st_atime_ns, object_stamp.st_mtime_ns))
                        self.cli(command, '--clean', '--jobs', '2', '-t', target)
                        self.assertTrue(any(executable.read_bytes()), 'executable was not recreated')
                        if object_file:
                            self.assertTrue(any(object_file.read_bytes()), 'transitive object was not recompiled')
                        self.assertEqual(optional_bytes, optional.read_bytes())
                        self.assertEqual(optional_stamp, optional.stat().st_mtime_ns)
                        self.assertEqual(snapshots, {path: path.read_bytes() for path in snapshots})
                self.cli('run', '--clean', '-t', 'base', success=False)
                self.assertEqual(optional_stamp, optional.stat().st_mtime_ns)

    def test_clean_build_preserves_other_profile(self):
        self.manifest(targets='[[target]]\nname="app"\nsources=["apps/optional.cpp"]\n')
        self.configure()
        self.cli('build')
        debug_output = self.build_path('app')
        debug_stamp = debug_output.stat().st_mtime_ns
        debug_bytes = debug_output.read_bytes()
        self.configure(release=True)
        self.cli('build', '--release')
        release_output = self.build_path('app')
        release_stamp = release_output.stat()
        release_output.write_bytes(bytes(release_stamp.st_size))
        os.utime(release_output, ns=(release_stamp.st_atime_ns, release_stamp.st_mtime_ns))
        self.cli('run', '--release', '--clean', '--jobs', '2')
        self.assertEqual(debug_bytes, debug_output.read_bytes())
        self.assertEqual(debug_stamp, debug_output.stat().st_mtime_ns)

    def test_clean_run_does_not_launch_after_build_failure(self):
        self.manifest(targets='[[target]]\nname="app"\nsources=["apps/optional.cpp"]\n')
        self.write('apps/optional.cpp', '#include <fstream>\n'
                   'int main() { std::ofstream("was-run").put(\'x\'); return 0; }\n')
        self.configure()
        self.cli('build')
        self.write('apps/optional.cpp', '#error intentional compilation failure\n')
        self.cli('run', '--clean', success=False)
        self.assertFalse((self.root / 'was-run').exists())

    def test_native_generators(self):
        """The same DAG builds, runs, stages DLLs and stays incremental everywhere."""
        fixture_root = self.root
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.root = fixture_root / generator
                self.root.mkdir()
                self.create_fixture()
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
                self.assertTrue(sentinel in archive.read_bytes(),
                                'initial archive is missing the fixture sentinel')
                self.manifest(generator, self.targets.replace(', "base/stale.cpp"', ''))
                self.configure(generator)
                self.cli('build', '-t', 'base')
                self.assertFalse(sentinel in archive.read_bytes(),
                                 f'{generator} retained a removed archive member')
                self.cli('clean')

    @unittest.skipIf(os.name == 'nt', 'GNU Make timestamp regression runs on POSIX')
    def test_make_archive_membership_with_equal_timestamps(self):
        """Removing a member rebuilds the archive even when metadata mtimes tie."""
        if not (shutil.which('make') or shutil.which('gmake')):
            self.skipTest('GNU Make is not installed')
        self.manifest('make')
        self.configure('make')
        self.cli('build', '-t', 'base')
        archive, = self.artifacts('base')
        sentinel = REMOVED_SYMBOL.encode('ascii')
        self.assertTrue(sentinel in archive.read_bytes())
        original_stamp = archive.stat().st_mtime_ns

        self.manifest('make', self.targets.replace(', "base/stale.cpp"', ''))
        self.configure('make')
        # Reproduce a coarse clock deterministically instead of sleeping until
        # the next second. A timestamp-only dependency check skips this change.
        self.tie_make_timestamps(original_stamp)
        self.cli('build', '-t', 'base')
        self.assertFalse(sentinel in archive.read_bytes(),
                         'equal timestamps hid the removed archive member')
        stamp = archive.stat().st_mtime_ns
        self.configure('make')
        self.cli('build', '-t', 'base')
        self.assertEqual(stamp, archive.stat().st_mtime_ns,
                         'unchanged configuration rebuilt the archive')

    @unittest.skipIf(os.name == 'nt', 'GNU Make timestamp regression runs on POSIX')
    def test_make_changed_flags_with_equal_timestamps(self):
        """New compiler definitions take effect even when response mtimes tie."""
        if not (shutil.which('make') or shutil.which('gmake')):
            self.skipTest('GNU Make is not installed')
        cc = self.coarse_clock_compiler(OPTIONS.cc, 'coarse-c-compiler')
        cxx = self.coarse_clock_compiler(OPTIONS.cxx, 'coarse-cxx-compiler')
        with mock.patch.object(OPTIONS, 'cc', cc), mock.patch.object(OPTIONS, 'cxx', cxx):
            self.check_make_changed_flags_with_equal_timestamps()

    def check_make_changed_flags_with_equal_timestamps(self):
        """Exercise a static diamond where rebuilt objects retain the old tick."""
        # Only the provider's private command changes. Consumers keep identical
        # commands, so their refreshed outputs require dependency invalidation.
        self.targets = self.targets.replace('defines = ["BASE_PRIVATE"]',
                          f'defines = ["BASE_PRIVATE", "BASE_PRIVATE_VALUE={BASE_API_VALUE}"]')
        self.write('base/base.cpp', (self.root / 'base/base.cpp').read_text().replace(
                   '{BASE_API}', '{BASE_PRIVATE_VALUE}'))
        self.write('apps/main.c', '#include "api.h"\n#include <stdio.h>\n'
                   'int main(void) { printf("%d\\n", answer()); return 0; }\n')
        self.manifest('make')
        self.configure('make')
        self.assertIn(str(SHARED_API_VALUE), self.cli('run', '-t', 'app').stdout.splitlines())
        original_stamp = self.build_path('app').stat().st_mtime_ns
        changed_base = BASE_API_VALUE + 1
        changed_shared = changed_base * DIAMOND_BRANCH_COUNT
        targets = self.targets.replace(f'BASE_PRIVATE_VALUE={BASE_API_VALUE}',
                                       f'BASE_PRIVATE_VALUE={changed_base}')
        self.manifest('make', targets)
        self.configure('make')
        self.write('coarse-clock', str(original_stamp))
        self.tie_make_timestamps(original_stamp)
        self.assertIn(str(changed_shared), self.cli('run', '-t', 'app').stdout.splitlines())
        stamp = self.build_path('app').stat().st_mtime_ns
        self.configure('make')
        self.assertIn(str(changed_shared), self.cli('run', '-t', 'app').stdout.splitlines())
        self.assertEqual(stamp, self.build_path('app').stat().st_mtime_ns,
                         'unchanged configuration relinked a consumer')

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
        self.cli('test')  # Restores saved configuration in a separate process.

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
        """Reject invalid dependency graphs, target identities and field types."""
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

    @unittest.skipUnless(sys.platform.startswith('linux'), 'ELF loader regression')
    def test_shared_runtime_survives_build_tree_relocation(self):
        """ELF dependencies use loader names and relative rpaths, not build paths."""
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator)
                self.configure(generator)
                self.cli('build', '-t', 'app', '-j', '1')
                application = self.build_path('app')
                original = application.parent
                moved = self.root / 'relocated artifacts'
                original.rename(moved)
                try:
                    result = subprocess.run([str(moved / application.name)], cwd=self.root,
                                            capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                finally:
                    moved.rename(original)
                self.cli('clean')

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

    def test_project_application_defaults_with_explicit_libraries(self):
        """Project entry/sources coexist with libraries and feed a named application."""
        library = self.targets.split('[[target]]\nname = "left"')[0]
        self.write('entry.cpp', '#include "base.h"\nint extra();\n'
                   'int main() { return base() == BASE_API && extra() == 1 ? 0 : 1; }\n')
        self.write('extra.cpp', 'int extra() { return 1; }\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator, library)
                text = (self.root / 'clutch.toml').read_text()
                text = text.replace('[build]', 'entry="entry.cpp"\n[build]\n'
                                    'sources=["extra.cpp"]\nlinks=["base"]')
                self.write('clutch.toml', text)
                self.configure(generator)
                self.cli('run', '-t', 'target_probe')
                self.assertIn('target_probe', self.configured_targets)
                self.assertEqual(len(self.configured_targets['target_probe']['sources']), 2)
                self.cli('clean')

                # An explicit executable with the project name can omit sources
                # and use the project entry while adding target-local settings.
                self.write('clutch.toml', text + '\n[[target]]\nname="target_probe"\n')
                self.configure(generator)
                self.cli('run', '-t', 'target_probe')
                self.cli('clean')

    def test_build_links_enables_discovery_beside_library_targets(self):
        """Application links opt into discovery without requiring an entry path."""
        (self.root / 'src').mkdir()
        self.write('src/main.cpp', '#include "base.h"\nint main() { return base() == BASE_API ? 0 : 1; }\n')
        library = self.targets.split('[[target]]\nname = "left"')[0]
        self.manifest(targets=library)
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace(
            '[build]', '[build]\nlinks=["base"]'))
        self.configure()
        self.cli('run', '-t', 'target_probe')

    def test_application_links_and_source_defaults_are_validated(self):
        """Mixing declarations preserves type, dependency and empty-source checks."""
        library = self.targets.split('[[target]]\nname = "left"')[0]
        self.manifest(targets=library)
        original = (self.root / 'clutch.toml').read_text()
        for setting, diagnostic in (
                ('links="base"', 'array'),
                ('links=[42]', 'strings'),
                ('links=["missing"]', 'unknown linked target'),
                ('links=["base"]', 'sources')):
            with self.subTest(setting=setting):
                manifest = original.replace('[build]', '[build]\n' + setting)
                if 'missing' in setting:
                    manifest = manifest.replace('[build]', 'entry="apps/optional.cpp"\n[build]')
                self.write('clutch.toml', manifest)
                result = self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                                  '--build-tool', OPTIONS.ninja, success=False)
                self.assertIn(diagnostic, result.stderr)

    def test_local_compile_overrides_and_public_interfaces(self):
        """Targets specialize defaults without duplicate definitions or lost exports."""
        for folder, value in (('defaults', 1), ('local', 2)):
            (self.root / folder).mkdir()
            self.write(folder + '/config.h', f'#define HEADER_VALUE {value}\n')
        self.write('entry.cpp', '#include <config.h>\n'
                   '#if VALUE != 3 || HEADER_VALUE != 2\n'
                   '#error incorrect compile overrides\n#endif\nint main() { return 0; }\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator, '[[target]]\nname="target_probe"\n'
                              'include_dirs=["local"]\ndefines=["VALUE=3"]\n'
                              'public_defines=["VALUE=2"]\n')
                self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace(
                    '[build]', 'entry="entry.cpp"\n[build]\ninclude_dirs=["defaults", "local"]\n'
                    'defines=["VALUE=1"]'))
                self.configure(generator)
                self.cli('run')
                arguments = self.configured_targets['target_probe']['sources'][0]['arguments']
                self.assertEqual(sum(arg.startswith(('-DVALUE=', '/DVALUE='))
                                     for arg in arguments), 1)
                self.cli('clean')

    def test_application_defaults_reject_real_name_conflicts(self):
        """An executable and a library still need distinct target identities."""
        library = self.targets.split('[[target]]\nname = "left"')[0]
        self.manifest(targets=library.replace('name = "base"', 'name = "target_probe"'))
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace(
            '[build]', 'entry="apps/optional.cpp"\n[build]'))
        result = self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                          '--build-tool', OPTIONS.ninja, success=False)
        self.assertIn('conflict', result.stderr)

    @unittest.skipIf(os.name == 'nt', 'POSIX executable-script fixture')
    def test_make_prefers_explicit_gnu_executable(self):
        """An installed gmake takes precedence over a host's legacy system make."""
        executable = shutil.which('gmake') or shutil.which('make')
        if not executable:
            self.skipTest('GNU Make is not installed')
        tools = self.root / 'tool bin'
        tools.mkdir()
        wrapper = tools / 'gmake'
        wrapper.write_text(f'#!{sys.executable}\nimport os,sys\n'
                           f'os.execv({executable!r}, [{executable!r}, *sys.argv[1:]])\n', encoding='utf-8')
        wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
        self.manifest('make')
        with mock.patch.dict(os.environ, {'PATH': str(tools) + os.pathsep + os.environ['PATH']}):
            os.environ.pop('MAKE', None)
            os.environ.pop('CLUTCH_BUILD_TOOL', None)
            self.configure('make')
        cache = self.build_path('app').parent / 'backend.toml'
        self.assertIn(str(wrapper), cache.read_text(encoding='utf-8'))

    @unittest.skipIf(os.name == 'nt', 'POSIX executable-script fixture')
    def test_legacy_make_is_rejected_before_generating_rules(self):
        """Unsupported timestamp precision must fail instead of hiding rebuilds."""
        fake = self.root / 'legacy-make'
        fake.write_text(f'#!{sys.executable}\nimport sys\n'
                        'assert sys.argv[1:] == ["--version"]\nprint("GNU Make 3.81")\n', encoding='utf-8')
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        self.manifest('make')
        result = self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                          '--build-tool', str(fake), success=False)
        self.assertIn('requires GNU Make 4 or newer', result.stderr)
        self.assertFalse(list(self.root.rglob('Makefile')))

    def test_recursive_source_patterns(self):
        """Recursive suffix sets deduplicate units and expose uncompiled headers."""
        (self.root / 'pattern lib/nested').mkdir(parents=True)
        (self.root / 'headers').mkdir()
        self.write('pattern lib/value.cpp', '#include "../headers/version.h"\n'
                   'int value() { return SELECTED_VALUE; }\n')
        self.write('pattern lib/value.h', '#error headers must not be compiled\n')
        self.write('pattern lib/nested/value.c', 'int nested(void) { return 0; }\n')
        self.write('pattern lib/nested/detail.hxx', '#pragma once\n')
        self.write('pattern lib/nested/ignored.txt', 'not C++\n')
        target = ('[[target]]\nname="library"\ntype="static"\n'
                  'sources=["pattern lib/*.{cpp,c,h,hxx}", "headers/*.h", "pattern lib/value.cpp"]\n'
                  '[[test]]\nname="pattern_test"\nsources=["checks/*.cpp"]\n'
                  'links=["library"]\n')
        self.write('checks/check.cpp', 'extern "C" int nested(void);\nint value();\n'
                   'int main() { return value() + nested(); }\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.write('headers/version.h', '#define SELECTED_VALUE 0\n')
                self.manifest(generator, target)
                self.configure(generator)
                library = self.configured_targets['library']
                compiled = [Path(unit['path']) for unit in library['sources']]
                self.assertEqual(len(compiled), len(set(compiled)))
                self.assertEqual(len(compiled), 2)
                self.assertIn(self.root / 'pattern lib/nested/value.c', compiled)
                self.assertEqual(set(map(Path, library['additionalFiles'])),
                                 {self.root / 'pattern lib/value.h', self.root / 'pattern lib/nested/detail.hxx',
                                  self.root / 'headers/version.h'})
                self.cli('build')
                self.cli('test')
                self.write('headers/version.h', '#define SELECTED_VALUE 1\n')
                stamp = max(path.stat().st_mtime_ns for name in ('library', 'pattern_test')
                            for path in self.artifacts(name)) + NANOSECONDS_PER_SECOND
                os.utime(self.root / 'headers/version.h', ns=(stamp, stamp))
                result = self.cli('test', success=False)
                self.assertIn('1 failed', result.stdout + result.stderr)
                self.cli('clean')

    def test_application_source_patterns_skip_generated_trees(self):
        """Broad scans keep generated and version-control files out of builds."""
        (self.root / 'sources/nested').mkdir(parents=True)
        self.write('sources/main.cpp', 'int helper(); int main() { return helper(); }\n')
        self.write('sources/nested/helper.cpp', 'int helper() { return 0; }\n')
        self.write('sources/nested/value.hpp', '#pragma once\n')
        self.write('sources/readme.txt', 'visible IDE file\n')
        for directory, marker in (('generated', 'CMakeCache.txt'),
                                  ('previous', '.clutch-build-owner'),
                                  ('.clutch', None), ('.git', None)):
            (self.root / 'sources' / directory).mkdir()
            self.write(f'sources/{directory}/broken.cpp', '#error generated input\n')
            if marker:
                self.write(f'sources/{directory}/{marker}', '')
        self.manifest(targets='', build_dir='sources/output')
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text(encoding='utf-8').replace(
            '[build]', '[build]\nsources=["sources/*", "sources/**/*.cpp"]'))
        self.configure()
        app = self.configured_targets['target_probe']
        self.assertEqual(len(app['sources']), 2)
        self.assertEqual(set(map(Path, app['additionalFiles'])),
                         {self.root / 'sources/nested/value.hpp', self.root / 'sources/readme.txt'})
        self.cli('run')
        location = json.loads(self.cli('locate').stdout)
        database = json.loads(Path(location['paths']['compilationDatabase']).read_text(encoding='utf-8'))
        self.assertEqual(len(database), 2)
        self.write('sources/nested/new.cpp', 'int added() { return 1; }\n')
        self.configure()
        self.assertEqual(len(self.configured_targets['target_probe']['sources']), 3)
        self.cli('build')

    def test_invalid_source_patterns(self):
        """Empty and malformed selections report the manifest pattern itself."""
        for pattern, diagnostic in (('base/*.missing', 'matched no files'),
                                    ('base/*.{cpp,}', 'empty source-pattern'),
                                    ('base/*.{cpp,c', 'invalid source-pattern'),
                                    ('base/*.{c,{cpp,h}}', 'invalid source-pattern'),
                                    ('base/*/nested.cpp', 'wildcards belong'),
                                    ('', 'must not be empty')):
            with self.subTest(pattern=pattern):
                self.manifest(targets='[[target]]\nname="library"\ntype="static"\n'
                                     f'sources=[{json.dumps(pattern)}]\n')
                result = self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                                  '--build-tool', OPTIONS.ninja, success=False)
                self.assertIn(diagnostic, result.stderr)

    def test_entry_discovery_excludes_declared_target_sources(self):
        """Adding a library does not compile its discovered sources into the app."""
        (self.root / 'src').mkdir()
        self.write('src/main.cpp', 'int helper();\nint main() { return helper(); }\n')
        self.write('src/helper.cpp', 'int helper() { return 0; }\n')
        self.manifest(targets='[[target]]\nname="helper"\ntype="static"\n'
                             'sources=["src/helper.cpp"]\n')
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace(
            '[build]', 'entry="src/main.cpp"\n[build]\nlinks=["helper"]'))
        self.configure()
        self.cli('run', '-t', 'target_probe')
        self.assertEqual(len(self.configured_targets['target_probe']['sources']), 1)
        location = json.loads(self.cli('locate').stdout)
        database = json.loads(Path(location['paths']['compilationDatabase']).read_text())
        self.assertEqual(sum(Path(unit['file']) == self.root / 'src/helper.cpp' for unit in database), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *arguments])
