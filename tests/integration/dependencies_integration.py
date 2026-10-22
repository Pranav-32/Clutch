#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise real local Git histories, conflict resolution and native PCH builds."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile

# Bound the test-only Git-hook handshake independently of compiler timestamps.
CHECKOUT_HANDSHAKE_TIMEOUT_SECONDS = 30
CHECKOUT_HOOK_POLL_SECONDS = 0.05
CHECKOUT_OBSERVER_POLL_SECONDS = 0.01
# Win32 API values used through ctypes; no pywin32 runtime is required.
WINDOWS_GENERIC_READ = 0x80000000
WINDOWS_SHARE_READ_WRITE = 3
WINDOWS_OPEN_EXISTING = 3
WINDOWS_NORMAL_ATTRIBUTES = 0x80


class DependencyTests(unittest.TestCase):
    """Fixtures use local repositories so tests never require a remote service."""

    def setUp(self):
        # MSBuild deliberately excludes the system TEMP tree from read tracking.
        # Windows fixtures therefore use the ignored workspace area. POSIX
        # fixtures use local temporary storage, avoiding slow bind-mount I/O.
        work = None
        if os.name == 'nt':
            work = Path(__file__).resolve().parents[1] / 'work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='dependencies ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.app = self.root / 'app'
        self.app.mkdir()
        self.write(self.app, 'main.cpp', 'int main() { return 0; }\n')

    def write(self, root, name, contents):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding='utf-8')

    def remove(self, path):
        """Delete only a fixture descendant, including Git's read-only objects."""
        self.assertTrue(path.resolve().is_relative_to(self.root) and path.resolve() != self.root)

        def writable_retry(operation, name, error):
            if os.name != 'nt' or not isinstance(error[1], PermissionError):
                raise error[1]
            os.chmod(name, stat.S_IREAD | stat.S_IWRITE)
            operation(name)

        shutil.rmtree(path, onerror=writable_retry)

    def git(self, root, *args):
        result = subprocess.run(['git', '-c', 'core.autocrlf=false', '-c', 'commit.gpgsign=false',
                                 '-c', 'tag.gpgsign=false', *args], cwd=root,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.strip()

    def repo(self, name):
        root = self.root / name
        root.mkdir()
        self.git(root, 'init', '-q')
        self.git(root, 'config', 'user.name', 'Clutch Tests')
        self.git(root, 'config', 'user.email', 'tests@example.invalid')
        return root

    def tag(self, root, version, annotated=False):
        self.git(root, 'add', '.')
        self.git(root, 'commit', '-qm', 'Publish fixture ' + version)
        if annotated:
            self.git(root, 'tag', '-a', '-m', 'Publish fixture ' + version, version)
        else:
            self.git(root, 'tag', version)
        return self.git(root, 'rev-parse', 'HEAD')

    def dependency(self, root, **settings):
        fields = {'git': root.as_posix(), **settings}
        return '{ ' + ', '.join(f'{key} = {json.dumps(value)}' for key, value in fields.items()) + ' }'

    def manifest(self, imports, generator='ninja', extra=''):
        self.write(self.app, 'clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\nentry="main.cpp"\n'
                   f'[build]\ngenerator="{generator}"\n{extra}\n'
                   '[profile.debug]\nbuild_dir="out"\n'
                   '[dependencies]\n' + '\n'.join(f'{name} = {value}' for name, value in imports.items()) + '\n')

    def cli(self, *args, success=True):
        result = subprocess.run([OPTIONS.clutch, *args], cwd=self.app, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, generator='ninja', *extra, success=True):
        args = ['.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx]
        if generator == 'ninja':
            args += ['--build-tool', OPTIONS.ninja]
        return self.cli(*args, *extra, success=success)

    def test_native_transitive_dependency_pins_and_offline_build(self):
        base = self.repo('base')
        self.write(base, 'include/answer.hpp', '#pragma once\n#define ANSWER 42\n')
        base_pin = self.tag(base, 'v1.0.0', annotated=True)
        library = self.repo('library')
        self.write(library, 'prefix.hpp', '#pragma once\n#define LIB_PREFIX 1\n')
        self.write(library, 'value.cpp', '#include <answer.hpp>\n#ifdef ROOT_PRIVATE\n#error leaked root flags\n#endif\n'
                   '#ifndef LIB_PRIVATE\n#error missing private flag\n#endif\n'
                   '#ifndef LIB_PREFIX\n#error missing native library PCH\n#endif\n'
                   'consteval int result() { return ANSWER; }\nint value() { return result(); }\n')
        self.write(library, 'clutch.toml', '[project]\nname="value"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=20\n[build]\ndefines=["LIB_PRIVATE"]\npch=["prefix.hpp"]\n'
                   '[[target]]\nname="value"\ntype="static"\n'
                   'sources=["v?lue.cpp"]\npublic_defines=["LIB_PUBLIC=1"]\n[dependencies]\nbase=' +
                   self.dependency(base, version='^1.0.0') + '\n')
        library_pin = self.tag(library, 'v1.0.0')
        self.write(self.app, 'main.cpp', '#include <answer.hpp>\n#ifdef LIB_PRIVATE\n#error private flag leaked\n#endif\n'
                   '#ifndef LIB_PUBLIC\n#error missing public flag\n#endif\nint value();\n'
                   'int main() { return value() == ANSWER ? 0 : 1; }\n')
        self.manifest({'value': self.dependency(library, tag='v1.0.0')}, extra='defines=["ROOT_PRIVATE"]')
        query = self.app / 'out/.clutch/api/v1/query'
        query.mkdir(parents=True)
        (query / 'codemodel-v1').touch()
        self.configure()
        models = list((self.app / 'out/.clutch/api/v1/reply').glob('generation-*/codemodel.json'))
        self.assertEqual(len(models), 1)
        model = json.loads(models[0].read_text())
        self.assertEqual(len(model['packages']), 2)
        self.assertTrue(any(item['dependencies'] for item in model['packages']))
        if dot := shutil.which('dot'):
            result = subprocess.run([dot, '-Tsvg', str(self.app / 'out/build-graph.dot')], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
        lock = (self.app / 'clutch.lock').read_text()
        self.assertIn(base_pin, lock)
        self.assertIn(library_pin, lock)
        self.assertIn('version = 2', lock)
        self.cli('run', '--jobs', '2')
        self.configure('ninja', '--locked', '--offline')
        self.assertEqual(lock, (self.app / 'clutch.lock').read_text())
        self.assertIn('no work to do', self.cli('build', '--jobs', '2').lower())
        # A later upstream tag must not silently replace an existing lock.
        self.write(base, 'include/answer.hpp', '#pragma once\n#define ANSWER 43\n')
        self.tag(base, 'v1.1.0')
        self.configure()
        self.assertEqual(lock, (self.app / 'clutch.lock').read_text())
        checkout = next((self.app / '.clutch/deps').glob(f'*/{base_pin}/source'))
        self.write(checkout, 'include/answer.hpp', '#error modified checkout\n')
        self.assertIn('modified', self.configure(success=False))

    def test_compatible_intersections_and_conflicts(self):
        base = self.repo('base')
        self.write(base, 'include/value.hpp', '#define VALUE 1\n')
        old = self.tag(base, 'v1.0.0')
        self.write(base, 'include/value.hpp', '#define VALUE 2\n')
        new = self.tag(base, 'v1.5.0')
        self.manifest({'a': self.dependency(base, version='^1.0.0'),
                       'b': self.dependency(base, version='>=1.2.0 <2.0.0')})
        self.configure()
        self.assertIn(new, (self.app / 'clutch.lock').read_text())
        self.assertNotIn(old, (self.app / 'clutch.lock').read_text())
        self.manifest({'a': self.dependency(base, tag='v1.0.0'),
                       'b': self.dependency(base, version='>=1.2.0 <2.0.0')})
        error = self.configure(success=False)
        self.assertIn('version conflict', error)
        self.assertIn('app -> a', error)
        self.assertIn('app -> b', error)

    def test_backtracking_reconsiders_transitive_choices(self):
        base = self.repo('base')
        self.write(base, 'include/value.hpp', '#define VALUE 1\n')
        pin = self.tag(base, 'v1.0.0')
        self.write(base, 'include/value.hpp', '#define VALUE 2\n')
        self.tag(base, 'v2.0.0')
        bridge = self.repo('bridge')
        for version, constraint in [('v1.0.0', '^1.0.0'), ('v1.1.0', '^2.0.0')]:
            self.write(bridge, 'clutch.toml', '[dependencies]\nbase=' +
                       self.dependency(base, version=constraint) + '\n')
            self.write(bridge, 'include/bridge.hpp', '#pragma once\n')
            self.tag(bridge, version)
        self.manifest({'base': self.dependency(base, version='^1.0.0'),
                       'bridge': self.dependency(bridge, version='^1.0.0', build_system='header-only')})
        self.configure()
        lock = (self.app / 'clutch.lock').read_text()
        self.assertIn(pin, lock)
        self.assertIn('refs/tags/v1.0.0', lock)
        self.assertNotIn('refs/tags/v1.1.0', lock)

    def test_header_only_cycles_are_rejected(self):
        left, right = self.repo('left'), self.repo('right')
        for root, other in [(left, right), (right, left)]:
            self.write(root, 'clutch.toml', '[dependencies]\nother=' +
                       self.dependency(other, build_system='header-only') + '\n')
            self.write(root, 'include/value.hpp', '#pragma once\n')
            self.tag(root, 'v1.0.0')
        self.manifest({'left': self.dependency(left, build_system='header-only')})
        self.assertIn('dependency cycle', self.configure(success=False))

    def test_relative_self_import_is_a_cycle_without_repeated_downloads(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.write(library, 'clutch.toml', '[dependencies]\n'
                   'self={git=".", build_system="header-only"}\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0', build_system='header-only')})
        error = self.configure(success=False)
        self.assertIn('dependency cycle', error)
        self.assertEqual(error.count('Fetching'), 1)

    def test_pch_creation_reuse_and_header_invalidation(self):
        headers = self.repo('headers')
        self.write(headers, 'include/prefix.hpp', '#pragma once\n#include <vector>\n#define ANSWER 42\n')
        self.tag(headers, 'v1.0.0')
        program = 'int main() { std::vector<int> v{ANSWER}; return v[0] == 42 ? 0 : 1; }\n'
        self.write(self.app, 'local.hpp', '#pragma once\n#define LOCAL_VALUE 7\n')
        self.write(self.app, 'additional.hpp', '#pragma once\n#define ADDITIONAL_VALUE 9\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.write(self.app, 'main.cpp', program)
                if (self.app / 'out').exists():
                    shutil.rmtree(self.app / 'out')
                self.manifest({'headers': self.dependency(headers, tag='v1.0.0', pch=['include/prefix.hpp'])},
                              generator, 'pch=["local.hpp"]')
                query = self.app / 'out/.clutch/api/v1/query'
                query.mkdir(parents=True)
                (query / 'codemodel-v1').touch()
                self.configure(generator)
                targets = list((self.app / 'out/.clutch/api/v1/reply').glob('generation-*/target-*.json'))
                prefix = json.loads(targets[0].read_text())['precompiledHeaders'][0]
                self.assertEqual(prefix['language'], 'CXX')
                self.assertEqual(len(prefix['headers']), 2)
                self.assertEqual(prefix['managedByGenerator'], generator == 'xcode')
                database = json.loads((self.app / 'out/compile_commands.json').read_text())
                self.assertIn('text', ' '.join(database[0]['arguments']))
                self.cli('run', '--jobs', '2')
                artifacts = list((self.app / 'out').rglob('*.pch')) + list((self.app / 'out').rglob('*.gch'))
                self.assertTrue(artifacts, 'No binary PCH was produced')
                stamps = {file: file.stat().st_mtime_ns for file in artifacts}
                self.cli('build', '--jobs', '2')
                self.assertEqual(stamps, {file: file.stat().st_mtime_ns for file in artifacts})
                self.write(self.app, 'local.hpp', '#pragma once\n#define LOCAL_VALUE 8\n')
                self.cli('run', '--jobs', '2')
                self.assertTrue(any(file.stat().st_mtime_ns != stamp for file, stamp in stamps.items()))
                before = {file: file.stat().st_mtime_ns for file in artifacts}
                manifest = self.app / 'clutch.toml'
                manifest.write_text(manifest.read_text().replace('pch=["local.hpp"]',
                                    'pch=["local.hpp", "additional.hpp"]'))
                self.write(self.app, 'main.cpp', '#ifndef ADDITIONAL_VALUE\n#error missing extended prefix\n#endif\n' + program)
                self.configure(generator)
                self.cli('build', '--jobs', '2')
                self.assertTrue(any(file.stat().st_mtime_ns != stamp for file, stamp in before.items()),
                                'Changing the prefix list did not recreate its binary PCH')

    def test_c_and_cpp_library_pch_are_separate(self):
        self.write(self.app, 'prefix.h', '#pragma once\n#include <stdint.h>\n#define C_VALUE 42\n')
        self.write(self.app, 'prefix.hpp', '#pragma once\n#include <vector>\n')
        self.write(self.app, 'value.c', 'int value(void) { uint32_t v = C_VALUE; return (int)v; }\n')
        self.write(self.app, 'main.cpp', 'extern "C" int value(void);\n'
                   'int main() { std::vector<int> v{value()}; return v[0] == 42 ? 0 : 1; }\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
                if (self.app / 'out').exists():
                    shutil.rmtree(self.app / 'out')
                self.write(self.app, 'clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n'
                           'language="mixed"\nstd={c=17, cpp=17}\nentry="main.cpp"\n'
                           f'[build]\ngenerator="{generator}"\n'
                           'pch={c=["prefix.h"], cpp=["prefix.hpp"]}\n'
                           '[profile.debug]\nbuild_dir="out"\n'
                           '[[target]]\nname="value"\ntype="static"\nsources=["value.c"]\n'
                           '[[target]]\nname="app"\nlinks=["value"]\n')
                self.configure(generator)
                self.cli('run', '--jobs', '2')
                self.cli('build', '--jobs', '1')

    def generators(self):
        result = ['ninja']
        if os.name == 'nt':
            if shutil.which('nmake'):
                result.append('nmake')
            if Path(OPTIONS.cxx).stem.lower() == 'cl':
                result.append('visual-studio')
        else:
            if shutil.which('make'):
                result.append('make')
            if sys.platform == 'darwin' and shutil.which('xcodebuild'):
                result.append('xcode')
        return result

    def test_header_only_transitive_interfaces_and_locked_cache_repair(self):
        base = self.repo('base')
        self.write(base, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        self.tag(base, 'v1.0.0')
        bridge = self.repo('bridge')
        self.write(bridge, 'include/bridge.hpp', '#pragma once\n#include <value.hpp>\n')
        self.write(bridge, 'clutch.toml', '[dependencies]\nbase=' +
                   self.dependency(base, tag='v1.0.0') + '\n')
        self.tag(bridge, 'v1.0.0')
        self.manifest({'bridge': self.dependency(bridge, tag='v1.0.0', build_system='header-only')})
        self.write(self.app, 'main.cpp', '#include <bridge.hpp>\nint main() { return VALUE == 42 ? 0 : 1; }\n')
        self.configure()
        self.cli('run')
        lock = (self.app / 'clutch.lock').read_bytes()
        self.remove(self.app / '.clutch')
        self.assertIn('offline', self.configure('ninja', '--locked', '--offline', success=False))
        self.configure('ninja', '--locked')
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())

    def test_outdated_reports_compatible_updates_without_publishing_them(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 1\n')
        old = self.tag(library, 'v1.0.0')
        self.write(self.app, 'main.cpp', '#include <value.hpp>\nint main() { return VALUE == 1 ? 0 : 1; }\n')
        self.manifest({'library': self.dependency(library, version='^1.0.0')})
        self.configure()
        self.cli('run', '--jobs', '2')
        lock = (self.app / 'clutch.lock').read_bytes()
        configuration = (self.app / 'out/configuration.toml').read_bytes()
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 2\n')
        new = self.tag(library, 'v1.1.0')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 3\n')
        self.tag(library, 'v2.0.0')
        output = self.cli('outdated')
        self.assertIn('v1.1.0', output)
        self.assertNotIn('v2.0.0', output)
        self.assertIn('Available', output)
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())
        self.assertEqual(configuration, (self.app / 'out/configuration.toml').read_bytes())
        self.cli('run', '--jobs', '2')
        self.assertIn('v1.1.0', self.configure('ninja', '--locked', '--check-updates'))
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())
        self.assertIn(old, lock.decode())
        self.assertNotIn(new, lock.decode())
        self.configure('ninja', '--auto-update-dependencies')
        self.assertIn(new, (self.app / 'clutch.lock').read_text())

    def test_outdated_preserves_exact_tags_and_reports_unchanged_graphs(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        old = self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure()
        lock = (self.app / 'clutch.lock').read_bytes()
        self.write(library, 'include/value.hpp', '#pragma once\n#define NEW_VALUE 2\n')
        self.tag(library, 'v1.1.0')
        self.git(library, 'tag', '-f', 'v1.0.0')
        self.assertIn('no compatible Git dependency updates', self.cli('outdated'))
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())
        self.assertIn(old, lock.decode())

    def test_outdated_reports_untagged_head_changes(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library)})
        self.configure()
        lock = (self.app / 'clutch.lock').read_bytes()
        self.write(library, 'include/value.hpp', '#pragma once\n#define HEAD_CHANGED 1\n')
        self.git(library, 'add', '.')
        self.git(library, 'commit', '-qm', 'Move the untagged HEAD fixture')
        output = self.cli('outdated')
        self.assertIn('Available', output)
        self.assertIn('HEAD @', output)
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())

    def test_outdated_respects_transitive_conflicts(self):
        base, bridge = self.repo('base'), self.repo('bridge')
        self.write(base, 'include/value.hpp', '#pragma once\n')
        self.tag(base, 'v1.0.0')
        self.write(bridge, 'include/bridge.hpp', '#pragma once\n')
        self.write(bridge, 'clutch.toml', '[dependencies]\nbase=' +
                   self.dependency(base, version='^1.0.0') + '\n')
        self.tag(bridge, 'v1.0.0')
        self.manifest({'base': self.dependency(base, version='^1.0.0'),
                       'bridge': self.dependency(bridge, version='^1.0.0', build_system='header-only')})
        self.configure()
        lock = (self.app / 'clutch.lock').read_bytes()
        self.write(base, 'include/value.hpp', '#pragma once\n#define MAJOR 2\n')
        self.tag(base, 'v2.0.0')
        self.write(bridge, 'clutch.toml', '[dependencies]\nbase=' +
                   self.dependency(base, version='^2.0.0') + '\n')
        self.tag(bridge, 'v1.1.0')
        self.assertIn('no compatible Git dependency updates', self.cli('outdated'))
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())

    def test_outdated_reports_added_and_removed_transitive_dependencies(self):
        before, after, wrapper = self.repo('before'), self.repo('after'), self.repo('wrapper')
        for library in (before, after):
            self.write(library, 'include/optional.hpp', '#pragma once\n')
            self.tag(library, 'v1.0.0')
        self.write(wrapper, 'include/wrapper.hpp', '#pragma once\n')
        self.write(wrapper, 'clutch.toml', '[dependencies]\nchild=' +
                   self.dependency(before, tag='v1.0.0') + '\n')
        self.tag(wrapper, 'v1.0.0')
        self.manifest({'wrapper': self.dependency(wrapper, version='^1.0.0', build_system='header-only')})
        self.configure()
        lock = (self.app / 'clutch.lock').read_bytes()
        self.write(wrapper, 'clutch.toml', '[dependencies]\nchild=' +
                   self.dependency(after, tag='v1.0.0') + '\n')
        self.tag(wrapper, 'v1.1.0')
        output = self.cli('outdated')
        self.assertIn(before.as_posix(), output)
        self.assertIn(after.as_posix(), output)
        self.assertIn('-> (not required)', output)
        self.assertIn('(not required) ->', output)
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())

    def test_update_checks_validate_options_and_require_a_lock(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, version='^1.0.0')})
        self.assertIn('clutch.lock', self.cli('outdated', success=False))
        for incompatible in ('--offline', '--auto-update-dependencies', '--targets'):
            self.assertIn('cannot be combined', self.configure('ninja', '--check-updates',
                          incompatible, success=False))
        self.assertFalse((self.app / '.clutch').exists())
        self.assertFalse((self.app / 'out').exists())
        self.manifest({})
        self.assertIn('no compatible Git dependency updates', self.cli('outdated'))
        self.assertFalse((self.app / '.clutch').exists())

    def test_outdated_reports_all_dependencies_removed_from_manifest(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library)})
        self.configure()
        lock = (self.app / 'clutch.lock').read_bytes()
        self.manifest({})
        self.assertIn('-> (not required)', self.cli('outdated'))
        self.assertEqual(lock, (self.app / 'clutch.lock').read_bytes())

    def test_opt_in_updates_ranges_and_preserves_exact_tags(self):
        base = self.repo('base')
        self.write(base, 'include/value.hpp', '#pragma once\n#define VALUE 1\n')
        old = self.tag(base, 'v1.0.0')
        self.manifest({'base': self.dependency(base, version='^1.0.0')})
        self.configure()
        self.write(base, 'include/value.hpp', '#pragma once\n#define VALUE 2\n')
        new = self.tag(base, 'v1.1.0')
        self.configure()
        self.assertIn(old, (self.app / 'clutch.lock').read_text())
        self.configure('ninja', '--auto-update-dependencies')
        self.assertIn(new, (self.app / 'clutch.lock').read_text())
        self.manifest({'base': self.dependency(base, tag='v1.0.0')})
        self.configure('ninja', '--auto-update-dependencies')
        self.assertIn(old, (self.app / 'clutch.lock').read_text())
        self.assertIn('cannot be combined', self.configure('ninja', '--locked',
                      '--auto-update-dependencies', success=False))

    def test_workspace_aliases_select_additional_library_targets(self):
        library = self.repo('library')
        self.write(library, 'value.cpp', 'int value() { return 40; }\n')
        self.write(library, 'extra.cpp', 'int extra() { return 2; }\n')
        self.write(library, 'clutch.toml', '[project]\nname="library"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\n[[target]]\nname="value"\ntype="static"\n'
                   'sources=["value.cpp"]\n[[target]]\nname="extra"\ntype="static"\nsources=["extra.cpp"]\n')
        self.tag(library, 'v1.0.0')
        self.write(self.app, 'clutch.toml', '[workspace]\nmembers=["client"]\n'
                   '[profile.debug]\nbuild_dir="out"\n')
        self.write(self.app, 'client/clutch.toml', '[project]\nname="client"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\nentry="main.cpp"\n[build]\nlinks=["dep::extra"]\n'
                   '[dependencies]\ndep=' + self.dependency(library, tag='v1.0.0', target='value') + '\n')
        self.write(self.app, 'client/main.cpp', 'int value(); int extra();\n'
                   'int main() { return value() + extra() == 42 ? 0 : 1; }\n')
        self.configure()
        self.cli('run', '--jobs', '2')

    def test_branch_updates_follow_current_ref_instead_of_cached_tags(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 1\n')
        old = self.tag(library, 'v1.0.0')
        branch = self.git(library, 'symbolic-ref', '--short', 'HEAD')
        self.manifest({'library': self.dependency(library, branch=branch)})
        self.configure()
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 2\n')
        new = self.tag(library, 'v1.1.0')
        self.configure()
        self.assertIn(old, (self.app / 'clutch.lock').read_text())
        self.configure('ninja', '--auto-update-dependencies')
        self.assertIn(new, (self.app / 'clutch.lock').read_text())

    def test_conflicts_cannot_move_a_previously_pinned_tag(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 1\n')
        old = self.tag(library, 'v1.0.0')
        self.manifest({'fixed': self.dependency(library, tag='v1.0.0')})
        self.configure()
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 2\n')
        self.tag(library, 'v1.1.0')
        self.git(library, 'tag', '-f', 'v1.0.0')
        self.configure('ninja', '--auto-update-dependencies')
        self.assertIn(old, (self.app / 'clutch.lock').read_text())
        self.manifest({'fixed': self.dependency(library, tag='v1.0.0'),
                       'ranged': self.dependency(library, version='>=1.1.0')})
        self.assertIn('version conflict', self.configure(success=False))

    def test_full_sha256_revision_uses_matching_repository_format(self):
        library = self.root / 'sha256 library'
        library.mkdir()
        result = subprocess.run(['git', 'init', '-q', '--object-format=sha256', str(library)],
                                capture_output=True, text=True)
        if result.returncode:
            self.skipTest('installed Git does not support SHA-256 repositories')
        self.git(library, 'config', 'user.name', 'Clutch Tests')
        self.git(library, 'config', 'user.email', 'tests@example.invalid')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        revision = self.tag(library, 'v1.0.0')
        self.assertEqual(len(revision), 64)
        self.manifest({'value': self.dependency(library, rev=revision)})
        self.configure()
        self.assertIn(revision, (self.app / 'clutch.lock').read_text())
        self.cli('build', '--jobs', '2')

    @unittest.skipUnless(os.name == 'nt', 'Windows non-delete-sharing file handles')
    def test_windows_publication_while_checkout_file_remains_open(self):
        """Hold a checked-out file until configure exits, without timed releases."""
        import ctypes
        from ctypes import wintypes

        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        revision = self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        ready, release = self.root / 'checkout-ready', self.root / 'checkout-release'
        template = self.root / 'git template'
        # Gate checkout completion so the file handle is open before Clutch can
        # publish anything. The hook writes only outside the checked-out tree.
        self.write(template, 'hooks/post-checkout', '#!/bin/sh\nset -eu\n' +
                   f'printf ready > {shlex.quote(ready.as_posix())}\n' +
                   f'while [ ! -e {shlex.quote(release.as_posix())} ]; do '
                   f'sleep {CHECKOUT_HOOK_POLL_SECONDS}; done\n')
        (template / 'hooks/post-checkout').chmod(0o755)
        environment = {**os.environ, 'GIT_CONFIG_COUNT': '1',
                       'GIT_CONFIG_KEY_0': 'init.templateDir',
                       'GIT_CONFIG_VALUE_0': str(template)}
        process = subprocess.Popen([OPTIONS.clutch, '.', '--cc', OPTIONS.cc,
                                    '--cxx', OPTIONS.cxx, '--build-tool', OPTIONS.ninja],
                                   cwd=self.app, env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = None
        try:
            # This bounds IPC setup, not filesystem timestamp ordering.
            deadline = time.monotonic() + CHECKOUT_HANDSHAKE_TIMEOUT_SECONDS
            while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(CHECKOUT_OBSERVER_POLL_SECONDS)
            self.assertTrue(ready.exists(), 'Git checkout did not reach its synchronization hook')
            candidates = list((self.app / '.clutch/deps').glob(
                f'*/{revision}/source/include/value.hpp'))
            # Include the earlier layout so this regression also proves that
            # directory publication fails in an unmodified release binary.
            candidates += list((self.app / '.clutch/download/source/include').glob('value.hpp'))
            self.assertEqual(len(candidates), 1)
            # GENERIC_READ, FILE_SHARE_READ|WRITE and OPEN_EXISTING intentionally
            # omit delete sharing for the lifetime of the configure process.
            handle = kernel.CreateFileW(str(candidates[0]), WINDOWS_GENERIC_READ,
                                       WINDOWS_SHARE_READ_WRITE, None, WINDOWS_OPEN_EXISTING,
                                       WINDOWS_NORMAL_ATTRIBUTES, None)
            self.assertNotEqual(handle, wintypes.HANDLE(-1).value, ctypes.get_last_error())
            release.write_text('continue', encoding='utf-8')
            output, _ = process.communicate(timeout=CHECKOUT_HANDSHAKE_TIMEOUT_SECONDS)
            self.assertEqual(process.returncode, 0, output)
            self.assertNotIn('could not roll back', output)
        finally:
            release.touch()
            if handle is not None and handle != wintypes.HANDLE(-1).value:
                kernel.CloseHandle(handle)
            if process.poll() is None:
                process.communicate(timeout=CHECKOUT_HANDSHAKE_TIMEOUT_SECONDS)
            if process.stdout:
                process.stdout.close()
        self.configure('ninja', '--locked', '--offline')

    def test_interrupted_verified_checkout_recovers_offline(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure()
        metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
        pending = metadata.parent.with_name(metadata.parent.name + '.pending.toml')
        original = metadata.read_bytes()
        lock = (self.app / 'clutch.lock').read_bytes()
        metadata.rename(pending)
        output = self.configure('ninja', '--locked', '--offline')
        self.assertNotIn('Fetching', output)
        self.assertEqual(metadata.read_bytes(), original)
        self.assertFalse(pending.exists())
        self.assertEqual((self.app / 'clutch.lock').read_bytes(), lock)

    def submodule_library(self):
        """A relative nested URL and moving branch must retain the gitlink pin."""
        leaf = self.repo('leaf')
        self.write(leaf, 'value.hpp', '#pragma once\n#define SUBMODULE_VALUE 42\n')
        leaf_pin = self.tag(leaf, 'v1.0.0')
        middle = self.repo('middle')
        self.git(middle, '-c', 'protocol.file.allow=always', 'submodule', 'add',
                 '../leaf', 'nested headers')
        self.tag(middle, 'v1.0.0')
        library = self.repo('submodule library')
        self.git(library, '-c', 'protocol.file.allow=always', 'submodule', 'add',
                 '../middle', 'vendor/middle')
        self.tag(library, 'v1.0.0')
        self.write(leaf, 'value.hpp', '#pragma once\n#define SUBMODULE_VALUE 99\n')
        self.tag(leaf, 'v1.1.0')
        self.write(self.app, 'main.cpp', '#include <value.hpp>\n'
                   'int main() { return SUBMODULE_VALUE == 42 ? 0 : 1; }\n')
        self.manifest({'library': self.dependency(library, git=library.as_uri(), tag='v1.0.0',
                       build_system='header-only',
                       include_dirs=['vendor/middle/nested headers'])})
        return leaf_pin

    def test_recursive_submodule_pins_offline_recovery_and_bundle(self):
        # Local file transport is enabled only for this local-repository fixture;
        # Clutch must never weaken Git's transport policy for downloaded projects.
        with patch.dict(os.environ, {'GIT_ALLOW_PROTOCOL': 'file'}):
            leaf_pin = self.submodule_library()
            self.configure()
            metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
            module = metadata.parent / 'source/vendor/middle/nested headers'
            self.assertEqual(self.git(module, 'rev-parse', 'HEAD'), leaf_pin)
            lock = (self.app / 'clutch.lock').read_bytes()
            pending = metadata.parent.with_name(metadata.parent.name + '.pending.toml')
            metadata.rename(pending)
            # Removing upstreams proves that offline validation and finalizing
            # an interrupted publication do not consult any remote repository.
            for name in ('leaf', 'middle', 'submodule library'):
                (self.root / name).rename(self.root / (name + '-unavailable'))
            self.configure('ninja', '--locked', '--offline')
            self.assertTrue(metadata.is_file())
            self.assertFalse(pending.exists())
            self.assertEqual((self.app / 'clutch.lock').read_bytes(), lock)
            self.cli('run')
            archive = self.root / 'submodules.zip'
            self.cli('bundle', '--output', str(archive))
            relocated = self.root / 'relocated'
            with zipfile.ZipFile(archive) as snapshot:
                self.assertFalse(any('.git' in Path(name).parts for name in snapshot.namelist()))
                snapshot.extractall(relocated)
            original = self.app
            self.app = relocated
            try:
                self.configure('ninja', '--offline')
                self.cli('run')
            finally:
                self.app = original

    def test_missing_submodules_are_recovered_online_but_never_fetched_offline(self):
        with patch.dict(os.environ, {'GIT_ALLOW_PROTOCOL': 'file'}):
            self.submodule_library()
            self.configure()
            metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
            source = metadata.parent / 'source'
            self.git(source, 'submodule', 'deinit', '--force', '--all')
            self.assertIn('submodule is unavailable offline',
                          self.configure('ninja', '--offline', '--locked', success=False))
            self.assertTrue(metadata.is_file())
            self.configure('ninja', '--locked')
            self.cli('run')

    def test_dirty_nested_submodule_is_preserved_even_with_ignore_all(self):
        with patch.dict(os.environ, {'GIT_ALLOW_PROTOCOL': 'file'}):
            self.submodule_library()
            self.configure()
            metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
            source = metadata.parent / 'source'
            self.git(source, 'config', 'submodule.vendor/middle.ignore', 'all')
            changed = source / 'vendor/middle/nested headers/value.hpp'
            changed.write_text('#error keep this edit\n', encoding='utf-8')
            self.assertIn('modified', self.configure(success=False))
            self.assertEqual(changed.read_text(), '#error keep this edit\n')

    def test_incomplete_owned_checkout_is_refetched_without_changing_pins(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure()
        metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
        pending = metadata.parent.with_name(metadata.parent.name + '.pending.toml')
        metadata.rename(pending)
        dirty = metadata.parent / 'source/incomplete.txt'
        dirty.write_text('unfinished checkout', encoding='utf-8')
        lock = (self.app / 'clutch.lock').read_bytes()
        self.assertIn('offline', self.configure('ninja', '--locked', '--offline', success=False))
        self.assertTrue(dirty.exists())
        self.configure('ninja', '--locked')
        self.assertTrue(metadata.is_file())
        self.assertFalse(pending.exists())
        self.assertFalse(dirty.exists())
        self.assertEqual((self.app / 'clutch.lock').read_bytes(), lock)

    def test_incomplete_checkout_without_ownership_is_preserved(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure()
        metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
        metadata.unlink()
        preserved = metadata.parent / 'keep.txt'
        preserved.write_text('unowned content', encoding='utf-8')
        self.assertIn('ownership', self.configure(success=False))
        self.assertEqual(preserved.read_text(), 'unowned content')

    def test_invalid_pending_records_preserve_unpublished_contents(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        revision = self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure()
        metadata, = (self.app / '.clutch/deps').glob('*/*/resolution.toml')
        pending = metadata.parent.with_name(metadata.parent.name + '.pending.toml')
        metadata.rename(pending)
        preserved = metadata.parent / 'source/keep.txt'
        preserved.write_text('do not discard', encoding='utf-8')
        # Ownership must be established before any partial directory is removed.
        maximum_metadata_bytes = 1024 * 1024
        cases = {
            'other source': f'git="other"\nrev={json.dumps(revision)}\n',
            'other revision': f'git={json.dumps(library.as_posix())}\nrev="' + '0' * 40 + '"\n',
            'malformed': 'invalid=[\n',
            'oversized': 'x' * (maximum_metadata_bytes + 1),
        }
        for name, content in cases.items():
            with self.subTest(record=name):
                pending.write_text(content, encoding='utf-8')
                before = pending.read_bytes()
                self.configure(success=False)
                self.assertEqual(preserved.read_text(), 'do not discard')
                self.assertEqual(pending.read_bytes(), before)
                self.assertFalse(metadata.exists())

    def test_legacy_download_leftovers_are_not_modified(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v1.0.0')
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        # Initialize an owned cache, then fetch a different revision with an old
        # download directory present. Its unrelated contents must be preserved.
        self.configure()
        legacy = self.app / '.clutch/download/keep.txt'
        legacy.parent.mkdir()
        legacy.write_text('retained legacy download', encoding='utf-8')
        self.write(library, 'include/value.hpp', '#pragma once\n#define NEXT 1\n')
        self.tag(library, 'v1.1.0')
        self.manifest({'library': self.dependency(library, tag='v1.1.0')})
        self.configure()
        self.assertEqual(legacy.read_text(), 'retained legacy download')

    def test_failed_checkout_rolls_back_read_only_git_objects(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v0.1.0')
        blob = self.git(library, 'hash-object', '-w', 'include/value.hpp')
        self.git(library, 'tag', 'v1.0.0', blob)
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure(success=False)
        self.assertFalse((self.app / '.clutch/download').exists())
        self.assertFalse(list((self.app / '.clutch/deps').glob('*.pending.toml')))
        self.assertFalse(list((self.app / '.clutch/deps').glob('*/*.pending.toml')))
        self.manifest({'library': self.dependency(library, tag='v0.1.0')})
        self.configure()

    def test_unknown_lock_fields_do_not_get_silently_overwritten(self):
        base = self.repo('base')
        self.write(base, 'include/value.hpp', '#pragma once\n')
        self.tag(base, 'v1.0.0')
        self.manifest({'base': self.dependency(base, tag='v1.0.0')})
        self.configure()
        lock = self.app / 'clutch.lock'
        modified = lock.read_text() + '\nunknown_field = "keep"\n'
        lock.write_text(modified)
        self.configure(success=False)
        self.assertEqual(modified, lock.read_text())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + name, required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
