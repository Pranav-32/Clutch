#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the public file API through the real native CLI on each CI host.

CTest supplies compiler and Ninja paths. Fixtures are isolated temporary projects;
no IDE, third-party Python module or Python runtime in Clutch is required.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock


class FileApiTests(unittest.TestCase):
    """Check independent consumers, ownership boundaries and published snapshots."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch file api ')
        self.addCleanup(self.temporary.cleanup)
        self.root = (Path(self.temporary.name).resolve() / 'project')
        self.root.mkdir()
        (self.root / 'src').mkdir()
        (self.root / 'headers').mkdir()
        (self.root / 'checks').mkdir()
        (self.root / 'headers/value.h').write_text(
            '#ifdef __cplusplus\nextern "C" {\n#endif\nint value(void);\n'
            '#ifdef __cplusplus\n}\n#endif\n', encoding='utf-8')
        (self.root / 'src/main.cpp').write_text(
            '#include "value.h"\nint main() { return value() == FLAG ? 0 : 1; }\n', encoding='utf-8')
        (self.root / 'src/value.c').write_text(
            '#include "value.h"\nint value(void) { return FLAG; }\n', encoding='utf-8')
        (self.root / 'checks/check.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
        self.manifest = self.root / 'clutch.toml'
        self.manifest.write_text(
            '[project]\nname = "api_probe"\nversion = "0.1.0"\n'
            'language = "mixed"\nstd = { c = 11, cpp = 17 }\n'
            '[build]\ninclude_dirs = ["headers"]\ndefines = ["FLAG=7"]\n'
            '[profile.debug]\nbuild_dir = "out/debug # profile"\n'
            '[profile.release]\nbuild_dir = "../external release"\n'
            '[[test]]\nname = "check_probe"\nsources = ["checks/check.cpp"]\n', encoding='utf-8')
        self.location = self.locate()
        self.build = Path(self.location['paths']['build'])
        self.query = Path(self.location['paths']['query'])
        self.reply = Path(self.location['paths']['reply'])

    def cli(self, *arguments, success=True):
        """Return complete diagnostics and require the expected process result."""
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                text=True, encoding='utf-8', errors='replace', capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def locate(self, release=False):
        return json.loads(self.cli('locate', '--project', str(self.root),
                                  *(['--release'] if release else [])).stdout)

    def configure(self, release=False, **kwargs):
        return self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        '--build-tool', OPTIONS.ninja,
                        *(['--release'] if release else []), **kwargs)

    def request(self, client='client-test', requests=None, content=None, location=None):
        directory = Path((location or self.location)['paths']['query'])
        path = directory / client / 'query.json'
        path.parent.mkdir(parents=True, exist_ok=True)
        if content is None:
            content = json.dumps({'requests': requests if requests is not None else [
                {'kind': kind, 'version': 1} for kind in ('codemodel', 'toolchains', 'inputs')]})
        path.write_text(content, encoding='utf-8')
        return path

    def index(self, reply=None):
        files = sorted((reply or self.reply).glob('index-*.json'))
        self.assertTrue(files, 'no API index was published')
        return files[-1], json.loads(files[-1].read_text(encoding='utf-8'))

    def load(self, reference, reply=None):
        directory = reply or self.reply
        path = directory / reference['jsonFile']
        self.assertTrue(path.resolve().is_relative_to(directory.resolve()))
        return json.loads(path.read_text(encoding='utf-8'))

    @unittest.skipIf(os.name == 'nt', 'POSIX symlinks; Windows junction checks run separately')
    def test_generated_links_cannot_overwrite_source(self):
        """Owned build metadata and compiler outputs must not follow source links."""
        source = self.root / 'src/main.cpp'
        expected = source.read_bytes()
        for name in ('build.ninja', 'compile_commands.json', 'configuration.toml',
                     'toolchain.toml', 'backend.toml'):
            with self.subTest(output=name):
                self.configure()
                output = self.build / name
                output.unlink()
                output.symlink_to(source)
                self.configure(success=False)
                self.assertEqual(source.read_bytes(), expected)
                output.unlink()

        self.configure()
        database = json.loads((self.build / 'compile_commands.json').read_text(encoding='utf-8'))
        output = Path(database[0]['output'])
        output.symlink_to(source)
        self.cli('build', '-j', '1', success=False)
        self.assertEqual(source.read_bytes(), expected)

    def test_unicode_paths_and_saved_actions(self):
        """Native paths, manifest paths and JSON remain UTF-8 across configuration/build."""
        renamed = self.root.with_name('caf\u00e9')
        self.root.rename(renamed)
        self.root = renamed
        manifest = self.root / 'clutch.toml'
        (self.root / 'src').rename(self.root / 'sourc\u00e9')
        text = manifest.read_text(encoding='utf-8').replace('[build]',
            '[build]\nsources=["sourc\u00e9/main.cpp", "sourc\u00e9/value.c"]')
        text = text.replace('out/debug # profile', 'out/d\u00e9bug')
        manifest.write_text(text, encoding='utf-8')
        self.location = self.locate()
        self.build = Path(self.location['paths']['build'])
        self.query = Path(self.location['paths']['query'])
        self.reply = Path(self.location['paths']['reply'])
        self.request()
        self.configure()
        tools = self.root / 'tools'
        tools.mkdir()
        executable = tools / Path(OPTIONS.clutch).name
        shutil.copy2(OPTIONS.clutch, executable)
        # Keep external launchers beside their installed assets (Chocolatey
        # shims are not relocatable). Clutch is the binary we can relocate.
        environment = {
            'CLUTCH_BUILD_TOOL': Path(OPTIONS.ninja).name,
            'PATH': os.pathsep.join((str(tools), str(Path(OPTIONS.ninja).parent),
                                    os.environ.get('PATH', ''))),
        }
        with mock.patch.object(OPTIONS, 'clutch', str(executable)), \
                mock.patch.dict(os.environ, environment):
            self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx)
            self.cli('run', '-j', '1')
            self.cli('test', '-j', '1')
        # Native Windows generators must decode rule filenames as well as
        # compiler response arguments; ASCII-only fixtures cannot catch this.
        if os.name == 'nt' and all(Path(compiler).stem.lower() == 'cl'
                                   for compiler in (OPTIONS.cc, OPTIONS.cxx)):
            for generator, tool in (('nmake', 'nmake'), ('visual-studio', 'MSBuild')):
                if executable := shutil.which(tool):
                    with self.subTest(generator=generator):
                        self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                                 '--generator', generator, '--build-tool', executable)
                        self.cli('run', '-j', '1')
                        self.cli('test', '-j', '1')
        database = json.loads((self.build / 'compile_commands.json').read_text(encoding='utf-8'))
        self.assertTrue(all(Path(unit['file']).is_file() for unit in database))
        _, index = self.index()
        self.assertEqual(Path(index['paths']['source']), self.root)
        self.assertEqual(index['status'], 'success')
        self.cli('clean')

    def test_capabilities_and_locate_do_not_configure(self):
        capabilities = json.loads(self.cli('capabilities').stdout)
        self.assertEqual(capabilities['fileApi']['version'], {'major': 1, 'minor': 0})
        self.assertEqual({item['kind'] for item in capabilities['fileApi']['objectKinds']},
                         {'codemodel', 'toolchains', 'inputs'})
        self.assertFalse(self.build.exists())
        self.configure()
        self.assertFalse((self.build / '.clutch').exists())
        self.assertFalse((self.build / '.clutch/api').exists())
        self.assertFalse((self.root / '.clutch').exists())
        self.assertFalse((self.root / '.clangd').exists())
        self.assertFalse((self.root / '.vscode').exists())

    def test_query_before_first_configure_and_exact_commands(self):
        query = self.request()
        original = query.read_bytes()
        self.configure()
        _, index = self.index()
        self.assertEqual(index['status'], 'success')
        self.assertEqual(query.read_bytes(), original)
        objects = {item['kind']: self.load(item) for item in index['objects']}
        model = objects['codemodel']
        self.assertEqual(model['profile'], 'debug')
        self.assertEqual(model['project']['name'], 'api_probe')
        database = json.loads(Path(model['compilationDatabase']).read_text(encoding='utf-8'))
        groups = []
        for target_reference in model['targets']:
            target = self.load(target_reference)
            self.assertFalse(Path(target['artifacts'][0]['path']).exists(), 'configure compiled a target')
            self.assertEqual(target['type'], 'EXECUTABLE')
            self.assertEqual(target['dependencies'], [])
            for source in target['sources']:
                record = next(item for item in database if Path(item['output']) == Path(source['object']))
                self.assertEqual(source['arguments'], record['arguments'])
                self.assertEqual(Path(source['path']), Path(record['file']))
                self.assertEqual(Path(target['directory']), Path(record['directory']))
                group = target['compileGroups'][source['compileGroupIndex']]
                self.assertIn('FLAG=7', group['defines'])
                self.assertEqual(Path(group['includes'][0]['path']), self.root / 'headers')
                groups.append((group['language'], group['standard']))
        self.assertIn(('C', '11'), groups)
        self.assertIn(('CXX', '17'), groups)
        self.assertEqual(len(model['targets']), 2)
        self.assertEqual({item['language'] for item in objects['toolchains']['toolchains']}, {'C', 'CXX'})
        self.assertEqual(Path(objects['inputs']['configurationInputs'][0]['path']), self.manifest)
        self.assertFalse((self.root / 'compile_commands.json').exists())

    def test_shared_and_multiple_client_queries(self):
        self.query.mkdir(parents=True)
        (self.query / 'codemodel-v1').touch()
        (self.query / 'unknown-v1').touch()
        client = self.query / 'client-other'
        client.mkdir()
        (client / 'inputs-v1').touch()
        metadata = {'session': 'caf\u00e9 \U0001f680', 'nested': {'kind': 'opaque', 'version': 1},
                    'large': 18446744073709551615}
        self.request(content=json.dumps({'requests': [
            {'kind': 'toolchains', 'version': [8, {'major': 1, 'minor': 0}], 'client': metadata},
            {'kind': 'inputs', 'version': 9}, {'kind': 'missing', 'version': 1},
            {'kind': 'codemodel', 'version': True}], 'client': metadata}))
        self.configure()
        _, index = self.index()
        self.assertEqual(index['reply']['codemodel-v1']['version']['major'], 1)
        responses = index['reply']['client-test']['query.json']['responses']
        self.assertEqual(responses[0]['client'], metadata)
        self.assertEqual(index['reply']['client-test']['query.json']['client'], metadata)
        self.assertEqual(responses[1]['error']['code'], 'unsupported-version')
        self.assertEqual(responses[2]['error']['code'], 'unknown-kind')
        self.assertEqual(responses[3]['error']['code'], 'invalid-request')
        self.assertEqual(self.load(index['reply']['client-other']['inputs-v1'])['kind'], 'inputs')

    def test_unrecognized_filenames_do_not_masquerade_as_references(self):
        self.request()
        (self.query / 'kind').touch()
        (self.query / 'version').touch()
        client = self.query / 'client-odd'
        client.mkdir()
        (client / 'kind').touch()
        (client / 'version').touch()
        self.configure()
        _, index = self.index()
        self.assertEqual(index['status'], 'success')
        self.assertEqual(index['reply']['kind']['error']['code'], 'unknown-query')
        self.assertEqual(index['reply']['client-odd']['version']['error']['code'], 'unknown-query')

    def test_removed_queries_publish_an_empty_snapshot(self):
        request = self.request()
        self.configure()
        old_path, old = self.index()
        request.unlink()
        self.configure()
        path, current = self.index()
        self.assertGreater(path.name, old_path.name)
        self.assertEqual(current['objects'], [])
        for reference in old['objects']:
            self.load(reference)

    def test_metadata_matches_other_native_generators(self):
        self.request()
        generators = []
        if os.name != 'nt' and shutil.which('make'):
            generators.append(('make', shutil.which('make')))
        if sys.platform == 'darwin' and shutil.which('xcodebuild'):
            generators.append(('xcode', shutil.which('xcodebuild')))
        if os.name == 'nt' and Path(OPTIONS.cxx).stem.lower() in ('cl', 'clang-cl'):
            for generator, tool in (('nmake', 'nmake'), ('visual-studio', 'MSBuild')):
                if generator == 'visual-studio' and any(
                        Path(compiler).stem.lower() != 'cl' for compiler in (OPTIONS.cc, OPTIONS.cxx)):
                    continue
                if shutil.which(tool):
                    generators.append((generator, shutil.which(tool)))
        for generator, executable in generators:
            self.cli('.', '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                     '--generator', generator, '--build-tool', executable)
            _, index = self.index()
            model = self.load(next(item for item in index['objects'] if item['kind'] == 'codemodel'))
            self.assertEqual(model['generator']['name'], generator)
            self.assertEqual(len(model['targets']), 2)

    def test_strict_json_and_resource_limits(self):
        bad = ['{"requests":[],"requests":[]}', '{"requests":[],}',
               '{"requests":[],"client":"\\ud800"}',
               '{"requests":[],"client":' + '[' * 70 + '0' + ']' * 70 + '}',
               'x' * (1024 * 1024 + 1), json.dumps({'requests': [{}] * 129})]
        for number, content in enumerate(bad):
            self.request('client-bad-' + str(number), content=content)
        invalid_utf8 = self.request('client-invalid-utf8')
        invalid_utf8.write_bytes(b'{"requests":[],"client":"\xff"}')
        self.request('client-valid')
        self.configure()
        _, index = self.index()
        self.assertEqual(index['status'], 'success')
        for number in range(len(bad)):
            self.assertEqual(index['reply']['client-bad-' + str(number)]['query.json']['error']['code'],
                             'invalid-query')
        self.assertEqual(len(index['objects']), 3)
        self.assertEqual(index['reply']['client-invalid-utf8']['query.json']['error']['code'], 'invalid-query')

    def test_invalid_version_tokens_are_not_coerced(self):
        invalid = [1.0, '1', False, -1, {'major': 1, 'minor': -1},
                   {'major': 8, 'minor': False}, [], [[]], None, {'minor': 0},
                   [1, True], {'major': 1, 'minor': 0.0}]
        self.request(requests=[{'kind': 'codemodel', 'version': value} for value in invalid])
        self.configure()
        _, index = self.index()
        for response in index['reply']['client-test']['query.json']['responses']:
            self.assertEqual(response['error']['code'], 'invalid-request')
        self.assertEqual(index['objects'], [])

    def test_batch_resource_limits_fail_configuration(self):
        self.request()
        self.configure()
        # Each directory is individually below its limit; the complete client
        # batch must still be bounded instead of multiplying the per-client cap.
        for name, count in (('client-many-one', 128), ('client-many-two', 129)):
            directory = self.query / name
            directory.mkdir()
            for number in range(count):
                (directory / ('unknown-' + str(number))).touch()
        self.configure(success=False)
        _, index = self.index()
        self.assertEqual(index['status'], 'error')
        self.assertIn('256 files', index['error']['message'])

    def test_profiles_external_directory_and_user_editor_files(self):
        self.request()
        release = self.locate(True)
        self.request(location=release)
        (self.root / '.clangd').write_bytes(b'# User settings\n')
        (self.root / '.vscode').mkdir()
        settings = self.root / '.vscode/c_cpp_properties.json'
        settings.write_bytes(b'// My editor settings\n{}\n')
        self.configure()
        _, debug_index = self.index()
        self.configure(True)
        _, release_index = self.index(Path(release['paths']['reply']))
        self.assertEqual(debug_index['profile'], 'debug')
        self.assertEqual(release_index['profile'], 'release')
        debug_model = self.load(next(item for item in debug_index['objects'] if item['kind'] == 'codemodel'))
        release_model = self.load(next(item for item in release_index['objects'] if item['kind'] == 'codemodel'),
                                  Path(release['paths']['reply']))
        self.assertEqual([target['id'] for target in debug_model['targets']],
                         [target['id'] for target in release_model['targets']])
        self.assertEqual(settings.read_bytes(), b'// My editor settings\n{}\n')
        self.assertEqual((self.root / '.clangd').read_bytes(), b'# User settings\n')

    def test_error_index_and_recovery(self):
        self.request()
        self.configure()
        old_path, _ = self.index()
        old = old_path.read_bytes()
        self.cli('.', '--cxx', str(self.root / 'missing-compiler'), success=False)
        error_path, error = self.index()
        self.assertGreater(error_path.name, old_path.name)
        self.assertEqual(error['status'], 'error')
        self.assertEqual(error['error']['code'], 'configure-failed')
        self.assertEqual(error['objects'], [])
        self.configure()
        self.assertEqual(self.index()[1]['status'], 'success')
        self.assertEqual(old_path.read_bytes(), old)

    def test_unrelated_unowned_directory_and_clean_require_owner(self):
        self.request()
        unrelated = self.build / 'do-not-delete.txt'
        unrelated.write_text('user data', encoding='utf-8')
        self.configure(success=False)
        self.assertEqual(unrelated.read_text(encoding='utf-8'), 'user data')
        self.assertFalse((self.build / '.clutch-build-owner').exists())
        unrelated.unlink()
        self.cli('clean', '--project', str(self.root), success=False)
        self.assertTrue(self.query.exists())
        self.configure()
        self.cli('clean', '--project', str(self.root))
        self.assertFalse(self.build.exists())

    def test_writer_lock_and_incomplete_generation(self):
        self.request()
        self.configure()
        previous, _ = self.index()
        lock = self.build / '.clutch/api/v1/configure.lock'
        lock.mkdir()
        self.configure(success=False)
        self.assertTrue(lock.is_dir(), 'another writer lock was removed')
        self.assertEqual(self.index()[0], previous)
        lock.rmdir()
        orphan = self.reply / 'generation-00000000000000000050'
        orphan.mkdir()
        (orphan / 'partial.json').write_text('{', encoding='utf-8')
        self.configure()
        self.assertGreater(self.index()[1]['generation'], '00000000000000000050')
        self.assertEqual((orphan / 'partial.json').read_text(encoding='utf-8'), '{')

    def test_published_indices_remain_readable_during_reconfigure(self):
        self.request()
        self.configure()
        errors = []
        stop = threading.Event()

        def observe():
            try:
                while not stop.is_set():
                    for path in self.reply.glob('index-*.json'):
                        index = json.loads(path.read_text(encoding='utf-8'))
                        for reference in index['objects']:
                            obj = self.load(reference)
                            for target in obj.get('targets', []):
                                self.load(target)
            except Exception as error:
                errors.append(error)

        thread = threading.Thread(target=observe)
        thread.start()
        try:
            for _ in range(3):
                self.configure()
        finally:
            stop.set()
            thread.join()
        self.assertEqual(errors, [])

    def test_emitted_build_and_run_actions(self):
        self.request()
        self.configure()
        _, index = self.index()
        model = self.load(next(item for item in index['objects'] if item['kind'] == 'codemodel'))
        for target_reference in model['targets']:
            target = self.load(target_reference)
            for name in ('build', 'run'):
                action = target['actions'][name]
                completed = subprocess.run(action['arguments'], cwd=action['directory'],
                                           text=True, capture_output=True)
                self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_reference_client_and_stale_manifest(self):
        client = Path(__file__).resolve().parents[2] / 'examples/file-api/client.py'
        command = [sys.executable, str(client), '--clutch', OPTIONS.clutch,
                   '--project', str(self.root), '--cc', OPTIONS.cc,
                   '--cxx', OPTIONS.cxx, '--build-tool', OPTIONS.ninja]
        completed = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        snapshot = json.loads(completed.stdout)
        self.assertEqual(len(snapshot['targets']), 2)
        self.manifest.write_text(self.manifest.read_text(encoding='utf-8') + '\n# changed\n', encoding='utf-8')
        stale = subprocess.run(command + ['--read-only'], text=True, capture_output=True)
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn('manifest changed', stale.stderr)

    def test_protocol_schema_documents_have_resolvable_local_references(self):
        directory = Path(__file__).resolve().parents[2] / 'docs/file-api'
        for path in directory.glob('*.schema.json'):
            document = json.loads(path.read_text(encoding='utf-8'))

            def check(value):
                if isinstance(value, dict):
                    if '$ref' in value:
                        self.assertTrue(value['$ref'].startswith('#/'))
                        current = document
                        for part in value['$ref'][2:].split('/'):
                            current = current[part]
                    for item in value.values():
                        check(item)
                elif isinstance(value, list):
                    for item in value:
                        check(item)

            check(document)

    @unittest.skipIf(os.name == 'nt', 'Windows Unicode CLI paths require the host UTF-8 environment')
    def test_unicode_source_root(self):
        renamed = self.root.with_name('project caf\u00e9 \U0001f680')
        self.root.rename(renamed)
        self.root = renamed
        self.manifest = self.root / 'clutch.toml'
        self.location = self.locate()
        self.build = Path(self.location['paths']['build'])
        self.query = Path(self.location['paths']['query'])
        self.reply = Path(self.location['paths']['reply'])
        self.request()
        self.configure()
        _, index = self.index()
        self.assertEqual(index['paths']['source'], self.root.as_posix())
        model = self.load(next(item for item in index['objects'] if item['kind'] == 'codemodel'))
        target = self.load(model['targets'][0])
        self.assertIn(self.root.as_posix(), target['sources'][0]['path'])

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink checks; Windows junctions are checked separately')
    def test_redirected_query_and_reply_paths(self):
        outside = Path(self.temporary.name) / 'outside'
        outside.mkdir()
        self.query.parent.mkdir(parents=True)
        self.query.symlink_to(outside, target_is_directory=True)
        self.configure(success=False)
        self.assertEqual(list(outside.iterdir()), [])
        self.query.unlink()
        self.request()
        self.configure()
        self.cli('clean', '--project', str(self.root))
        self.configure()
        self.query.parent.mkdir(parents=True)
        self.reply.symlink_to(outside, target_is_directory=True)
        self.configure(success=False)
        self.assertEqual(list(outside.iterdir()), [])

    @unittest.skipUnless(os.name == 'nt', 'Windows directory junction check')
    def test_windows_junction_is_rejected(self):
        outside = Path(self.temporary.name) / 'outside'
        outside.mkdir()
        self.query.parent.mkdir(parents=True)
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(self.query), str(outside)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        try:
            self.configure(success=False)
            self.assertEqual(list(outside.iterdir()), [])
        finally:
            self.query.rmdir()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clutch', required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--ninja', required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.clutch = str(Path(OPTIONS.clutch).resolve())
    unittest.main(argv=[sys.argv[0], *remaining])
