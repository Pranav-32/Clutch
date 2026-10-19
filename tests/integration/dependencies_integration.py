#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise real local Git histories, conflict resolution and native PCH builds."""
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
        self.write(self.app, 'main.cpp', 'int main() { std::vector<int> v{ANSWER}; return v[0] == 42 ? 0 : 1; }\n')
        self.write(self.app, 'local.hpp', '#pragma once\n#define LOCAL_VALUE 7\n')
        self.write(self.app, 'additional.hpp', '#pragma once\n#define ADDITIONAL_VALUE 9\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
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

    def test_failed_checkout_rolls_back_read_only_git_objects(self):
        library = self.repo('library')
        self.write(library, 'include/value.hpp', '#pragma once\n')
        self.tag(library, 'v0.1.0')
        blob = self.git(library, 'hash-object', '-w', 'include/value.hpp')
        self.git(library, 'tag', 'v1.0.0', blob)
        self.manifest({'library': self.dependency(library, tag='v1.0.0')})
        self.configure(success=False)
        self.assertFalse((self.app / '.clutch/download').exists())
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
