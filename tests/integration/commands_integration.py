#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise root commands, lifecycle ordering, failure gates and literal argv."""

import argparse
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
TIMEOUT = 120
PHASES = ('before-configure', 'after-configure', 'before-build', 'after-build')


class CommandTests(unittest.TestCase):
    """Use real Python commands as user tools; Clutch itself remains native."""

    def setUp(self):
        parent = ROOT / 'tests/work' if os.name == 'nt' else None
        if parent:
            parent.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='command runner ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.write('main.cpp', '#include <cstdio>\nint main() { puts("APP_RAN"); }\n')
        self.write('scripts/event recorder.py', '''import json
import os
from pathlib import Path
import sys

event, filename, root, profile = sys.argv[1:5]
print('CHILD ' + event, flush=True)
project = Path(root)
if (project / (event + '.fail')).exists():
    sys.exit(7)
if event == 'after-configure':
    assert (project / ('out-' + profile) / 'compile_commands.json').is_file()
if event == 'after-build':
    assert (project / ('out-' + profile) / ('app.exe' if os.name == 'nt' else 'app')).is_file()
if event == 'generate':
    (project / 'main.cpp').write_text('int main() { return 0; }\\n')
if event == 'header':
    (project / 'dynamic.hpp').write_text('#define BUILD_VALUE 42\\n')
path = Path(filename)
path.parent.mkdir(parents=True, exist_ok=True)
with path.open('a', encoding='utf-8') as output:
    output.write(json.dumps({'event': event, 'cwd': str(Path.cwd()),
                             'profile': profile, 'args': sys.argv[5:]}) + '\\n')
''')
        self.manifest()

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
        return path

    def declaration(self, name, phases=(), extra=(), cwd='.', event=None):
        argv = [sys.executable, '@PROJECT_DIR@/scripts/event recorder.py',
                event or name, '@PROJECT_DIR@/events.jsonl', '@PROJECT_DIR@',
                '@PROFILE@', *extra]
        return ('\n[[command]]\nname=' + json.dumps(name) + '\nargv=' +
                json.dumps(argv, ensure_ascii=False) + '\nworking_dir=' +
                json.dumps(cwd) + '\nphases=' + json.dumps(phases) + '\n')

    def manifest(self, declarations=None, project=None):
        if declarations is None:
            declarations = ''.join(self.declaration(phase, [phase]) for phase in PHASES)
        if project is None:
            project = '[project]\nname="app"\nversion="1.0.0"\nentry="main.cpp"\n'
        self.write('clutch.toml', project + '[profile.debug]\nbuild_dir="out-debug"\n'
                   '[profile.release]\nbuild_dir="out-release"\n' + declarations)

    def cli(self, *arguments, code=0):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                capture_output=True, text=True, timeout=TIMEOUT)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def configure(self, release=False, code=0):
        return self.cli('.', '--generator', 'ninja', '--build-tool', OPTIONS.ninja,
                        '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        *(['--release'] if release else []), code=code)

    def events(self):
        path = self.root / 'events.jsonl'
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def names(self):
        return [event['event'] for event in self.events()]

    def test_order_noop_and_clean_run(self):
        """Hooks wrap each CLI build once, including no-op and clean builds."""
        self.configure()
        self.assertEqual(self.names(), list(PHASES[:2]))
        self.cli('build', '--jobs', '2')
        self.cli('build', '--jobs', '2')
        result = self.cli('run', '--clean')
        self.assertIn('APP_RAN', result.stdout)
        self.assertEqual(self.names(), list(PHASES[:2]) + list(PHASES[2:]) * 3)

    def test_manual_argv_directory_and_sourceless_generation(self):
        """Manual commands do not need a compiler, sources or saved graph."""
        (self.root / 'main.cpp').unlink()
        (self.root / 'working dir').mkdir()
        self.manifest(self.declaration('generate', extra=['two words', '', '$HOME',
                                                          'x;y', '@@BUILD_DIR@@'],
                                       cwd='working dir'))
        result = self.cli('exec', 'generate', '--', '--literal', 'a b')
        self.assertLess(result.stdout.index('Running generate'),
                        result.stdout.index('CHILD generate'))
        event = self.events()[0]
        self.assertEqual(event['cwd'], str(self.root / 'working dir'))
        self.assertEqual(event['args'], ['two words', '', '$HOME', 'x;y', '@BUILD_DIR@',
                                        '--literal', 'a b'])
        self.assertTrue((self.root / 'main.cpp').is_file())
        self.assertFalse((self.root / 'out-debug').exists())
        self.configure()
        self.cli('run')

    def test_profile_context_and_build_directory_placeholder(self):
        """Expansion uses the selected profile without recursive substitutions."""
        self.manifest(self.declaration('context', extra=['@BUILD_DIR@'], cwd='@PROJECT_DIR@'))
        self.cli('exec', 'context', '--release')
        event = self.events()[0]
        self.assertEqual(event['profile'], 'release')
        self.assertEqual(event['args'], [str(self.root / 'out-release')])

    def test_before_build_can_generate_a_header(self):
        """A pre-build command's output must exist before consumers compile."""
        self.write('main.cpp', '#include "dynamic.hpp"\nint main() { return BUILD_VALUE-42; }\n')
        self.manifest(self.declaration('header', ['before-build']))
        self.configure()
        self.assertFalse((self.root / 'dynamic.hpp').exists())
        self.cli('run')
        self.assertEqual(self.names(), ['header'])

    def test_relative_executable_resolves_against_working_directory(self):
        """Native launch uses the declared cwd, not Clutch's parent cwd."""
        self.manifest('')
        self.configure()
        self.cli('build')
        name = 'tool.exe' if os.name == 'nt' else 'tool'
        directory = self.root / 'working dir'
        directory.mkdir()
        shutil.copy2(self.root / 'out-debug' / ('app.exe' if os.name == 'nt' else 'app'),
                     directory / name)
        self.manifest('[[command]]\nname="native"\nargv=["./' + name + '"]\n'
                      'working_dir="working dir"\n')
        self.assertIn('APP_RAN', self.cli('exec', 'native').stdout)

    def test_source_bundle_retains_portable_commands(self):
        """Relative scripts/context tokens follow an extracted source tree."""
        self.manifest(self.declaration('manual'))
        archive = self.root / 'snapshot.zip'
        self.cli('bundle', '--output', str(archive))
        self.assertEqual(self.events(), [])
        destination = self.root / 'extracted'
        with zipfile.ZipFile(archive) as package:
            package.extractall(destination)
        self.cli('exec', 'manual', '--project', str(destination))
        records = [json.loads(line) for line in (destination / 'events.jsonl').read_text().splitlines()]
        self.assertEqual(records[0]['cwd'], str(destination))
        self.assertEqual(self.events(), [])

    def test_before_build_failure_stops_compilation(self):
        """A failed pre-hook preserves its exit status and prevents later hooks."""
        self.configure()
        self.write('before-build.fail', '')
        self.cli('run', code=7)
        self.assertEqual(self.names(), list(PHASES[:2]))
        self.assertFalse(list((self.root / 'out-debug').glob('*.exe')))
        self.assertFalse((self.root / 'out-debug/app').exists())

    def test_after_build_failure_prevents_application_execution(self):
        """Post-build failures keep artifacts but do not launch the application."""
        self.configure()
        self.write('after-build.fail', '')
        result = self.cli('run', code=7)
        self.assertNotIn('APP_RAN', result.stdout)
        self.assertEqual(self.names(), list(PHASES[:3]))

    def test_compiler_failure_does_not_run_after_hook(self):
        """Post hooks run only after successful compilation and linking."""
        self.configure()
        self.write('main.cpp', '#error intentional compile failure\n')
        result = subprocess.run([OPTIONS.clutch, 'build'], cwd=self.root,
                                capture_output=True, text=True, timeout=TIMEOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.names(), list(PHASES[:3]))

    def test_configure_failure_blocks_old_state_and_reports_file_api(self):
        """Even unchanged manifests cannot reuse state after a failed hook."""
        location = json.loads(self.cli('locate').stdout)
        query = Path(location['paths']['query'])
        query.mkdir(parents=True, exist_ok=True)
        (query / 'codemodel-v1').touch()
        self.configure()
        for phase in PHASES[:2]:
            with self.subTest(phase=phase):
                marker = self.write(phase + '.fail', '')
                self.configure(code=7)
                result = self.cli('build', code=1)
                self.assertIn('configuration did not complete', result.stderr)
                reply = Path(location['paths']['reply'])
                index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text())
                self.assertEqual(index['status'], 'error')
                marker.unlink()
                self.configure()
        self.cli('run')

    def test_schema_validation_and_missing_tools(self):
        """Reject every malformed command before any hook executes."""
        valid = self.declaration('valid', ['before-configure'])
        for invalid in ('command="x"', '[[command]]\nname="x"\nargv=[]',
                        '[[command]]\nname="x"\nargv="echo x"',
                        '[[command]]\nname="x"\nargv=[1]',
                        '[[command]]\nname="x"\nargv=["echo"]\nphases=["before-build","before-build"]',
                        '[[command]]\nname="x"\nargv=["echo"]\nphases=["after-link"]',
                        '[[command]]\nname="x"\nargv=["@WRONG@"]',
                        '[[command]]\nname="x"\nargv=["echo"]\nworking_dir=""',
                        '[[command]]\nname="x"\nargv=["echo"]\nshell=true'):
            with self.subTest(invalid=invalid):
                self.manifest(invalid if invalid.startswith('command=') else valid + invalid)
                self.configure(code=1)
                self.assertEqual(self.events(), [])
        self.manifest(valid + valid)
        self.assertIn('duplicate command', self.cli('exec', 'valid', code=1).stderr)
        self.manifest('[[command]]\nname="missing"\nargv=["clutch-no-such-tool-927ef"]\n')
        self.assertIn('executable not found', self.cli('exec', 'missing', code=1).stderr)
        self.assertIn('unknown project command', self.cli('exec', 'absent', code=1).stderr)

    def test_workspace_root_hooks_run_once(self):
        """Root commands wrap the workspace, rather than each member target."""
        (self.root / 'main.cpp').unlink()
        self.write('member/main.cpp', 'int main() { return 0; }\n')
        self.write('member/clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n')
        # This fixture's after-build assertion expects a root artifact; select
        # an ordinary recorder for workspace lifecycle ordering instead.
        self.manifest(''.join(self.declaration(phase, [phase], event='workspace-' + phase)
                              for phase in PHASES),
                      project='[workspace]\nmembers=["member"]\n')
        self.configure()
        self.cli('build', '--jobs', '2')
        self.assertEqual(self.names(), ['workspace-' + phase for phase in PHASES])
        self.write('member/clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n' +
                   self.declaration('member-command'))
        self.assertIn('root owns command', self.configure(code=1).stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        value = getattr(OPTIONS, option)
        setattr(OPTIONS, option, os.path.abspath(shutil.which(value) or value))
    unittest.main(argv=[sys.argv[0], *arguments])
