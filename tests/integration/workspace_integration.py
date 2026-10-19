#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Validate member composition and portable locks using actual native builds."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


# Leave a full clock tick between a manifest edit and native timestamp rules,
# including filesystems/tools that compare modification times in whole seconds.
TIMESTAMP_ORDERING_MARGIN_NS = 1_000_000_000


class WorkspaceTests(unittest.TestCase):
    """Every fixture has independently scoped C/C++ manifests and local links."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch workspace ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        for folder in ('base', 'bridge', 'app'):
            (self.root / folder).mkdir()
        self.manifest()
        self.write('base/clutch.toml', '[project]\nname="base"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\n[build]\ndefines=["BASE_PRIVATE"]\n'
                   '[[target]]\nname="value"\ntype="static"\nsources=["value.cpp"]\n')
        self.write('base/value.cpp', '#include <string_view>\n#ifndef BASE_PRIVATE\n#error scope lost\n#endif\n'
                   'int value() { return std::string_view("abc").size() * 7; }\n')
        self.write('bridge/clutch.toml', '[project]\nname="bridge"\nversion="2.0.0"\n'
                   'language="cpp"\nstd=20\n[[target]]\nname="api"\ntype="shared"\n'
                   'sources=["api.cpp"]\nlinks=["base::value"]\npublic_include_dirs=["."]\n'
                   'defines=["API_BUILD"]\npublic_defines=["API_VERSION=2"]\n')
        self.write('bridge/api.h', '#pragma once\n#ifdef _WIN32\n#ifdef API_BUILD\n'
                   '#define API __declspec(dllexport)\n#else\n#define API __declspec(dllimport)\n'
                   '#endif\n#else\n#define API\n#endif\n#ifdef __cplusplus\nextern "C" {\n'
                   '#endif\nAPI int answer(void);\n#ifdef __cplusplus\n}\n#endif\n')
        self.write('bridge/api.cpp', '#include "api.h"\n#ifdef BASE_PRIVATE\n#error leaked private macro\n'
                   '#endif\nint value();\nconsteval int zero() { return 0; }\n'
                   'int answer(void) { return value() + zero(); }\n')
        self.app_manifest = '[project]\nname="app"\nversion="3.0.0"\nlanguage="c"\nstd=17\n'
        self.app_manifest += ('[[target]]\nname="local"\ntype="static"\nsources=["local.c"]\n'
                              '[[target]]\nname="value"\nsources=["main.c"]\n'
                              'links=["local", "bridge::api"]\n'
                              '[[test]]\nname="check"\nsources=["check.cpp"]\nlinks=["bridge::api"]\n')
        self.write('app/clutch.toml', self.app_manifest)
        self.write('app/local.c', 'int local(void) { return 1; }\n')
        self.write('app/main.c', '#include "api.h"\n#include <stdio.h>\n'
                   '#if __STDC_VERSION__ < 201710L\n#error wrong member C standard\n#endif\n'
                   'int local(void);\nint main(void) { FILE *f = fopen("member-data.txt", "r");\n'
                   'if (!f) return 4;\nfclose(f);\nreturn answer() + local() == 22 ? 0 : 1; }\n')
        self.write('app/member-data.txt', 'Working directory belongs to this member.\n')
        self.write('app/check.cpp', '#include "api.h"\nint main() { return answer() == 21 ? 0 : 1; }\n')

    def write(self, path, text):
        (self.root / path).write_text(text, encoding='utf-8')

    def manifest(self, generator='ninja'):
        self.write('clutch.toml', '[workspace]\nmembers=["base", "bridge", "app"]\n'
                   f'[build]\ngenerator="{generator}"\n'
                   '[profile.debug]\nbuild_dir="out debug"\n'
                   '[profile.release]\nbuild_dir="out release"\nwarnings="none"\n')

    def cli(self, *arguments, success=True):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def configure(self, generator='ninja', *extra, success=True):
        args = ['.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx]
        if generator == 'ninja':
            args += ['--build-tool', OPTIONS.ninja]
        return self.cli(*args, *extra, success=success)

    def generators(self):
        result = ['ninja']
        if os.name == 'nt':
            if shutil.which('nmake'):
                result.append('nmake')
            # Native Visual Studio projects require the Microsoft CL driver.
            if all(Path(compiler).stem.lower() == 'cl' for compiler in (OPTIONS.cc, OPTIONS.cxx)):
                result.append('visual-studio')
        else:
            if shutil.which('gmake') or shutil.which('make'):
                result.append('make')
            if sys.platform == 'darwin' and shutil.which('xcodebuild'):
                result.append('xcode')
        return result

    def test_native_generators_and_profiles(self):
        """Names, standards, runtime lookup and member cwd survive every backend."""
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator)
                self.configure(generator)
                listing = self.cli('targets').stdout
                self.assertIn('base::value', listing)
                self.assertIn('app::value', listing)
                self.cli('run', '-t', 'app::value')
                self.cli('test', '-t', 'app::check')
                self.cli('build', '-t', 'value', success=False)
                lock = self.root / 'clutch.lock'
                snapshot, stamp = lock.read_bytes(), lock.stat().st_mtime_ns
                self.configure(generator, '--locked')
                self.assertEqual(stamp, lock.stat().st_mtime_ns)
                self.configure(generator, '--release', '--locked')
                self.cli('run', '--release', '-t', 'app::value')
                self.assertEqual(snapshot, lock.read_bytes())
                for profile in ((), ('--release',)):
                    self.cli('clean', *profile)
                self.assertTrue(lock.exists(), 'clean removed portable resolution')

    def test_file_api_and_stale_members(self):
        """One model describes all members and fingerprints each parsed input."""
        location = json.loads(self.cli('locate').stdout)
        query = Path(location['paths']['query'])
        query.mkdir(parents=True)
        for kind in ('codemodel', 'inputs'):
            (query / (kind + '-v1')).touch()
        self.configure()
        reply = Path(location['paths']['reply'])
        index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text())
        objects = {ref['kind']: json.loads((reply / ref['jsonFile']).read_text()) for ref in index['objects']}
        self.assertTrue(objects['codemodel']['workspace'])
        self.assertEqual({p['name'] for p in objects['codemodel']['projects']}, {'base', 'bridge', 'app'})
        targets = {ref['name']: json.loads((reply / ref['jsonFile']).read_text())
                   for ref in objects['codemodel']['targets']}
        self.assertEqual(targets['bridge::api']['compileGroups'][0]['standard'], '20')
        self.assertEqual(targets['app::value']['compileGroups'][0]['standard'], '17')
        self.assertEqual(targets['app::value']['project'], 'app')
        self.assertEqual(Path(targets['app::value']['actions']['run']['directory']), self.root / 'app')
        self.assertEqual(len(objects['inputs']['configurationInputs']), 5)
        self.assertEqual(len(index['configurationInputs']), 5)
        database = json.loads(Path(location['paths']['compilationDatabase']).read_text())
        for target in targets.values():
            for unit in target['sources']:
                record = next(item for item in database if Path(item['output']) == Path(unit['object']))
                self.assertEqual(record['arguments'], unit['arguments'])
        member = self.root / 'base/clutch.toml'
        stamp = member.stat().st_mtime_ns
        self.write('base/clutch.toml', member.read_text() + '\n# changed\n')
        # Clutch validates content, even if a checkout/editor preserves mtimes.
        os.utime(member, ns=(stamp, stamp))
        self.cli('build', success=False)
        client = Path(__file__).resolve().parents[2] / 'examples/file-api/client.py'
        stale = subprocess.run([sys.executable, str(client), '--clutch', OPTIONS.clutch,
                                '--project', str(self.root), '--read-only'], capture_output=True, text=True)
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn('configuration input changed', stale.stderr)
        self.configure('ninja', '--locked')  # Comments do not change resolution.
        self.cli('test')

    def test_lock_gate_portability_and_writer(self):
        """Frozen configuration fails without rewriting missing or stale locks."""
        self.configure('ninja', '--locked', success=False)
        self.assertFalse((self.root / 'clutch.lock').exists())
        self.configure()
        lock = self.root / 'clutch.lock'
        old = lock.read_bytes()
        self.assertNotIn(str(self.root).encode(), old)
        self.assertNotIn(OPTIONS.cxx.encode(), old)
        # Git checkout line endings and comments do not change resolution.
        lock.write_bytes(b'# local comment\r\n' + old.replace(b'\n', b'\r\n'))
        old = lock.read_bytes()
        stamp = lock.stat().st_mtime_ns
        self.configure('ninja', '--locked')
        self.assertEqual(old, lock.read_bytes())
        self.assertEqual(stamp, lock.stat().st_mtime_ns)
        self.write('app/clutch.toml', self.app_manifest.replace('3.0.0', '3.1.0'))
        self.configure('ninja', '--locked', success=False)
        self.assertEqual(old, lock.read_bytes())
        writer = self.root / 'clutch.lock.write-lock'
        writer.mkdir()
        self.configure(success=False)
        self.assertEqual(old, lock.read_bytes())
        writer.rmdir()
        self.configure()
        self.assertNotEqual(old, lock.read_bytes())
        self.write('clutch.lock', 'version=999\n')
        self.configure(success=False)
        self.assertEqual(lock.read_text(), 'version=999\n')

    def test_native_regeneration_watches_member_manifests(self):
        """Direct Ninja/Make refresh lock/configuration after a child manifest edit."""
        for generator in ('ninja', 'make'):
            if generator == 'make' and (os.name == 'nt' or generator not in self.generators()):
                continue
            with self.subTest(generator=generator):
                self.manifest(generator)
                self.configure(generator)
                lock = self.root / 'clutch.lock'
                previous = lock.read_bytes()
                path = self.root / 'base/clutch.toml'
                text = path.read_text()
                path.write_text(text.replace('1.0.0', '1.0.1') if '1.0.0' in text
                                else text.replace('1.0.1', '1.0.0'))
                build = self.root / 'out debug'
                rules = build / ('build.ninja' if generator == 'ninja' else 'Makefile')
                previous_rules = rules.read_bytes()
                # Native tools schedule regeneration by mtime, not content.
                # Age the generated file instead of sleeping or future-dating
                # inputs, which can cause endless regeneration/clock-skew errors.
                stamp = path.stat().st_mtime_ns - TIMESTAMP_ORDERING_MARGIN_NS
                os.utime(rules, ns=(stamp, stamp))
                self.assertGreater(path.stat().st_mtime_ns, rules.stat().st_mtime_ns)
                command = [OPTIONS.ninja, '-C', str(build)] if generator == 'ninja' else [
                    shutil.which('gmake' if sys.platform == 'darwin' else 'make'), '-C', str(build)]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotEqual(previous, lock.read_bytes())
                self.assertTrue(previous_rules != rules.read_bytes(),
                                'generated rules did not record the new input snapshot')
                stamp = rules.stat().st_mtime_ns
                unchanged = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(unchanged.returncode, 0, unchanged.stdout + unchanged.stderr)
                self.assertNotIn('Configured', unchanged.stdout + unchanged.stderr,
                                 'unchanged native build repeated configuration')
                self.assertEqual(stamp, rules.stat().st_mtime_ns)
                self.cli('run', '-t', 'app::value')
                self.cli('clean')

    def test_double_quoted_lock_output_and_migration(self):
        """Normal configuration canonicalizes quoting; frozen checks preserve data."""
        self.configure()
        lock = self.root / 'clutch.lock'
        canonical = lock.read_bytes()
        self.assertIn(b'name = "base"', canonical)
        self.assertIn(b'dependencies = [ "base" ]', canonical)
        self.assertNotIn(b"'", canonical)
        self.assertFalse((self.root / 'out debug/.clutch/api').exists())
        self.assertFalse((self.root / '.clutch').exists())
        self.assertFalse((self.root / 'clutch.lock.write-lock').exists())

        # Old single-quoted locks remain usable with --locked, then migrate on
        # a normal configure without changing the stored resolution.
        literal = canonical.replace(b'"', b"'")
        lock.write_bytes(literal)
        self.configure('ninja', '--locked')
        self.assertEqual(literal, lock.read_bytes())
        self.configure()
        self.assertEqual(canonical, lock.read_bytes())

        # Exercise the formatter's real TOML escaping rather than replacing
        # quote characters, including a literal apostrophe inside a basic string.
        version = '3.0.0+it\'s "quoted"\\path\nnext'
        self.write('app/clutch.toml', self.app_manifest.replace('"3.0.0"', json.dumps(version)))
        self.configure()
        text = lock.read_text()
        self.assertIn('version = ' + json.dumps(version), text)
        self.configure('ninja', '--locked')

    def test_invalid_members_and_cross_project_cycles(self):
        """Reject ambiguous manifests, escaping paths, invalid ownership and cycles."""
        root = (self.root / 'clutch.toml').read_text()
        for members in ('["base", "base"]', '["."]', '["../outside"]', '[42]', '[]', '["missing"]'):
            self.write('clutch.toml', root.replace('["base", "bridge", "app"]', members))
            self.configure(success=False)
        self.write('clutch.toml', root)
        base = (self.root / 'base/clutch.toml').read_text()
        for extra in ('\n[profile.debug]\nwarnings="none"\n', '\n[workspace]\nmembers=["x"]\n'):
            self.write('base/clutch.toml', base + extra)
            self.configure(success=False)
        self.write('base/clutch.toml', base.replace('name="base"', 'name="app"'))
        self.configure(success=False)
        self.write('base/clutch.toml', base + 'links=["bridge::api"]\n')
        self.configure(success=False)

    def test_root_project_and_relocated_checkout(self):
        """A real root project is optional; portable lock bytes survive relocation."""
        root = (self.root / 'clutch.toml').read_text()
        self.write('root.cpp', 'int main() { return 0; }\n')
        self.write('clutch.toml', '[project]\nname="root"\nversion="1.0.0"\nstd=17\n'
                   + root + '\n[[target]]\nname="tool"\nsources=["root.cpp"]\n')
        self.configure()
        self.cli('run', '-t', 'root::tool')
        snapshot = (self.root / 'clutch.lock').read_bytes()
        self.cli('clean')
        moved = self.root.parent / (self.root.name + ' relocated')
        self.root.rename(moved)
        self.root = moved
        # TemporaryDirectory still owns the original name, so register moved cleanup.
        self.addCleanup(shutil.rmtree, moved)
        self.configure('ninja', '--locked')
        self.assertEqual(snapshot, (moved / 'clutch.lock').read_bytes())
        self.cli('run', '-t', 'app::value')

    def test_shared_loader_name_collisions_are_rejected(self):
        """Repeated local names are valid until distinct shared modules must coexist."""
        (self.root / 'other').mkdir()
        self.write('other/clutch.toml', '[project]\nname="other"\nversion="1"\n'
                   '[[target]]\nname="api"\ntype="shared"\nsources=["api.cpp"]\n')
        self.write('other/api.cpp', 'int other() { return 0; }\n')
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace(
            '"bridge",', '"bridge", "other",'))
        self.configure()
        self.write('app/clutch.toml', self.app_manifest.replace(
            '"local", "bridge::api"', '"local", "bridge::api", "other::api"'))
        result = self.configure(success=False)
        self.assertIn('runtime library name collision', result.stderr)
        self.assertIn('bridge::api', result.stderr)
        self.assertIn('other::api', result.stderr)

    def test_real_root_application_libraries_and_member_defaults(self):
        """A real root mixes shorthand, targets and members in one locked model."""
        (self.root / 'shared_headers').mkdir()
        self.write('shared_headers/root_config.h', '#define ROOT_HEADER 1\n')
        self.write('root.cpp', '#include <root_config.h>\n#include "api.h"\n'
                   '#if ROOT_HEADER != 1 || ROOT_VALUE != 1 || PUBLIC_VALUE != 2\n'
                   '#error root defaults or public interface lost\n#endif\n'
                   'int root_helper();\nint main() { return root_helper() + answer() == 22 ? 0 : 1; }\n')
        self.write('helper.cpp', '#include <root_config.h>\n'
                   '#if ROOT_VALUE != 2\n#error target override lost\n#endif\n'
                   'int root_helper() { return ROOT_HEADER; }\n')
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator)
                manifest = (self.root / 'clutch.toml').read_text().replace(
                    '[build]', '[project]\nname="root"\nversion="4.0.0"\nentry="root.cpp"\n'
                    'std=17\n[build]\nlinks=["helper", "bridge::api"]\n'
                    'include_dirs=["shared_headers"]\ndefines=["ROOT_VALUE=1", "PUBLIC_VALUE=1"]')
                manifest += ('\n[[target]]\nname="helper"\ntype="static"\n'
                             'sources=["helper.cpp"]\ndefines=["ROOT_VALUE=2"]\n'
                             'public_defines=["PUBLIC_VALUE=2"]\n')
                self.write('clutch.toml', manifest)
                query_location = json.loads(self.cli('locate').stdout)
                query = Path(query_location['paths']['query']) / 'codemodel-v1'
                query.parent.mkdir(parents=True, exist_ok=True)
                query.touch()
                self.configure(generator)
                self.cli('run', '-t', 'root::root')
                self.cli('test', '-t', 'app::check')
                snapshot = (self.root / 'clutch.lock').read_bytes()
                self.configure(generator, '--locked')
                self.assertEqual(snapshot, (self.root / 'clutch.lock').read_bytes())
                self.assertIn(b'dependencies = [ "bridge" ]', snapshot)
                reply = Path(query_location['paths']['reply'])
                index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text())
                model_ref = next(ref for ref in index['objects'] if ref['kind'] == 'codemodel')
                model = json.loads((reply / model_ref['jsonFile']).read_text())
                self.assertEqual({project['name'] for project in model['projects']},
                                 {'root', 'base', 'bridge', 'app'})
                database = json.loads(Path(query_location['paths']['compilationDatabase']).read_text())
                for ref in model['targets']:
                    target = json.loads((reply / ref['jsonFile']).read_text())
                    for source in target['sources']:
                        unit = next(unit for unit in database
                                    if Path(unit['output']) == Path(source['object']))
                        self.assertEqual(unit['arguments'], source['arguments'])
                        self.assertTrue(any(str(self.root / 'shared_headers') in arg
                                            for arg in unit['arguments']))
                self.cli('clean')

    def test_virtual_root_rejects_application_settings(self):
        """Shared compile settings do not create a project at a virtual root."""
        manifest = (self.root / 'clutch.toml').read_text()
        for setting in ('sources=[]', 'links=[]'):
            with self.subTest(setting=setting):
                self.write('clutch.toml', manifest.replace('[build]', '[build]\n' + setting))
                result = self.configure(success=False)
                self.assertIn('require a root [project]', result.stderr)

    def test_root_compile_defaults_and_local_overrides(self):
        """Virtual-root settings reach every member; local includes/macros win."""
        for folder in ('shared_headers', 'base/local_headers'):
            (self.root / folder).mkdir()
        self.write('shared_headers/workspace_config.h', '#define WORKSPACE_HEADER 1\n')
        self.write('base/local_headers/workspace_config.h', '#define WORKSPACE_HEADER 2\n')
        base = (self.root / 'base/clutch.toml').read_text()
        self.write('base/clutch.toml', base.replace('defines=["BASE_PRIVATE"]',
                   'include_dirs=["local_headers"]\ndefines=["BASE_PRIVATE", "VALUE=2"]')
                   + 'defines=["VALUE=3"]\n')
        for source in ('base/value.cpp', 'bridge/api.cpp', 'app/local.c', 'app/main.c', 'app/check.cpp'):
            expected_header = 2 if source.startswith('base/') else 1
            expected_value = 3 if source.startswith('base/') else 1
            self.write(source, '#include "workspace_config.h"\n'
                       '#ifndef WORKSPACE_FLAG\n#error missing workspace define\n#endif\n'
                       f'#if WORKSPACE_HEADER != {expected_header} || VALUE != {expected_value}\n'
                       '#error compile defaults have incorrect precedence\n#endif\n'
                       + (self.root / source).read_text())
        for generator in self.generators():
            with self.subTest(generator=generator):
                self.manifest(generator)
                self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace(
                    '[build]', '[build]\ninclude_dirs=["shared_headers"]\n'
                    'defines=["WORKSPACE_FLAG", "VALUE=1"]'))
                self.configure(generator)
                self.cli('run', '-t', 'app::value')
                self.cli('test', '-t', 'app::check')
                location = json.loads(self.cli('locate').stdout)
                database = json.loads(Path(location['paths']['compilationDatabase']).read_text())
                for unit in database:
                    definitions = [arg for arg in unit['arguments'] if arg.startswith(('-DVALUE=', '/DVALUE='))]
                    self.assertEqual(len(definitions), 1, 'overrides emitted conflicting macro flags')
                    self.assertTrue(any(str(self.root / 'shared_headers') in arg for arg in unit['arguments']))
                self.cli('clean')

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink check')
    def test_redirected_member_and_lock_are_rejected(self):
        (self.root / 'alias').symlink_to(self.root / 'base', target_is_directory=True)
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace('"base"', '"alias"'))
        self.configure(success=False)
        self.manifest()
        outside = self.root / 'outside.txt'
        outside.write_text('version=1\n')
        (self.root / 'clutch.lock').symlink_to(outside)
        self.configure(success=False)
        self.assertEqual(outside.read_text(), 'version=1\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *arguments])
