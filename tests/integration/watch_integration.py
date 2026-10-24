#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise a live watcher with real compilers, failures and concurrent edits."""

import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import json
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TIMEOUT = 60
QUIET = 0.45


class Session:
    """Read native child output continuously and bound every wait/cleanup."""

    def __init__(self, root, arguments):
        self.lines = []
        self.condition = threading.Condition()
        self.process = subprocess.Popen(
            [OPTIONS.clutch, 'watch', '--interval', '40', '--debounce', '120', *arguments],
            cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding='utf-8', errors='replace', bufsize=1,
            start_new_session=os.name != 'nt',
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0)
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        """Notify tests only after a complete flushed status/output line."""
        for line in self.process.stdout:
            with self.condition:
                self.lines.append(line.rstrip())
                self.condition.notify_all()
        with self.condition:
            self.condition.notify_all()

    def output(self):
        with self.condition:
            return '\n'.join(self.lines)

    def cycles(self):
        with self.condition:
            return sum(line.startswith('Watching ') for line in self.lines)

    def wait(self, predicate):
        """Fail with the entire captured trace if the watcher exits or stalls."""
        deadline = time.monotonic() + TIMEOUT
        with self.condition:
            while not predicate():
                remaining = deadline - time.monotonic()
                if self.process.poll() is not None or remaining <= 0:
                    raise AssertionError('watch exited or timed out:\n' + '\n'.join(self.lines))
                self.condition.wait(min(remaining, 0.2))

    def cycle(self, count):
        self.wait(lambda: self.cycles() >= count)

    def text(self, value):
        self.wait(lambda: value in self.output())

    def quiet(self):
        """Observe several full poll/debounce windows without another cycle."""
        previous = self.cycles()
        time.sleep(QUIET)
        assert self.process.poll() is None, self.output()
        assert self.cycles() == previous, self.output()

    def stop(self):
        """Stop the entire test process group if ordinary shutdown is unavailable."""
        if self.process.poll() is None:
            try:
                self.process.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt'
                                         else signal.SIGINT)
                self.process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                if os.name == 'nt':
                    self.process.kill()
                else:
                    os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=5)
        self.reader.join(timeout=2)
        self.process.stdout.close()


class WatchTests(unittest.TestCase):
    """Keep real edits and cycle counts independent of filesystem timestamps."""

    def setUp(self):
        parent = ROOT / 'tests/work' if os.name == 'nt' else None
        if parent:
            parent.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='watch integration ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name).resolve()
        self.root = self.base / 'project'
        self.root.mkdir()
        self.write('value.hpp', '#define VALUE 1\n')
        self.write('main.cpp', '#include "value.hpp"\n#include <cstdio>\n'
                   'int main(int argc, char **argv) { printf("VALUE:%d\\n", VALUE); '
                   'if (argc > 1) puts(argv[1]); }\n')
        self.manifest()

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
        return path

    def manifest(self, extra='', project=None):
        project = project if project is not None else (
            '[project]\nname="app"\nversion="1.0.0"\nentry="main.cpp"\n')
        # Initial watch configuration intentionally uses cached tool selections.
        self.write('clutch.toml', project + '[profile.debug]\nbuild_dir="out-debug"\n'
                   '[profile.release]\nbuild_dir="out-release"\n' + extra)

    def configure(self, release=False):
        result = subprocess.run([OPTIONS.clutch, '.', '--generator', 'ninja',
                                 '--build-tool', OPTIONS.ninja, '--cc', OPTIONS.cc,
                                 '--cxx', OPTIONS.cxx, *(['--release'] if release else [])],
                                cwd=self.root, capture_output=True, text=True, timeout=TIMEOUT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def watch(self, *arguments, configured=True):
        if configured:
            self.configure(release='--release' in arguments)
        session = Session(self.root, arguments)
        self.addCleanup(session.stop)
        session.cycle(1)
        session.quiet()
        return session

    def test_header_edits_and_unchanged_timestamps(self):
        """Content changes trigger builds even with identical size/mtime."""
        watcher = self.watch('--run', '--', 'two words')
        self.assertIn('VALUE:1', watcher.output())
        self.assertIn('two words', watcher.output())
        header = self.root / 'value.hpp'
        before = header.stat()
        self.write('value.hpp', '#define VALUE 2\n')
        os.utime(header, ns=(before.st_atime_ns, before.st_mtime_ns))
        watcher.cycle(2)
        self.assertIn('VALUE:2', watcher.output())
        watcher.quiet()

    def test_manifest_and_source_membership_reconfigure(self):
        """New/deleted units update the saved graph before the next build."""
        self.manifest(project='[project]\nname="app"\nversion="1.0.0"\n')
        watcher = self.watch('--run')
        self.write('extra.cpp', 'int extra() { return 42; }\n')
        watcher.cycle(2)
        self.assertIn('reconfiguring project', watcher.output())
        database = self.root / 'out-debug/compile_commands.json'
        self.assertTrue(any(Path(row['file']).name == 'extra.cpp'
                            for row in json.loads(database.read_text())))
        (self.root / 'extra.cpp').unlink()
        watcher.cycle(3)
        self.assertFalse(any(Path(row['file']).name == 'extra.cpp'
                             for row in json.loads(database.read_text())))
        self.write('clutch.toml', (self.root / 'clutch.toml').read_text().replace('1.0.0', '1.0.1'))
        watcher.cycle(4)
        watcher.quiet()

    def test_failure_and_recovery(self):
        """Compiler failures remain watchable; invalid TOML can recover too."""
        watcher = self.watch('--run')
        self.write('main.cpp', '#error deliberate watch test\n')
        watcher.cycle(2)
        self.assertIn('cycle failed', watcher.output())
        self.write('main.cpp', '#include <cstdio>\nint main() { puts("RECOVERED"); }\n')
        watcher.cycle(3)
        self.assertIn('RECOVERED', watcher.output())
        valid = (self.root / 'clutch.toml').read_text()
        self.write('clutch.toml', '[invalid TOML')
        watcher.cycle(4)
        self.write('clutch.toml', valid)
        watcher.cycle(5)
        watcher.quiet()

    def test_unconfigured_start_and_native_provider_stability(self):
        """Configure a fresh root and never switch providers during deletion."""
        self.write('CMakeLists.txt', '# Not a CMake application\n')
        with mock.patch.dict(os.environ, {'CC': OPTIONS.cc, 'CXX': OPTIONS.cxx,
                                          'CLUTCH_BUILD_TOOL': OPTIONS.ninja}):
            watcher = self.watch('--run', configured=False)
        self.assertIn('VALUE:1', watcher.output())
        manifest = (self.root / 'clutch.toml').read_text()
        (self.root / 'clutch.toml').unlink()
        watcher.cycle(2)
        self.assertIn('cycle failed', watcher.output())
        self.assertFalse(list(self.root.rglob('CMakeCache.txt')))
        self.write('clutch.toml', manifest)
        watcher.cycle(3)
        watcher.quiet()

    def test_bursts_outputs_and_ignored_paths(self):
        """Coalesce edits and prevent generated trees from feeding back."""
        (self.root / 'assets/ignored').mkdir(parents=True)
        watcher = self.watch('--path', 'assets', '--ignore', 'assets/ignored')
        self.write('out-debug/noise.cpp', '#error generated output\n')
        self.write('.clutch/deps/noise.hpp', 'cache\n')
        self.write('.git/noise.cpp', 'metadata\n')
        self.write('assets/ignored/file.txt', 'ignored\n')
        watcher.quiet()
        for value in range(2, 8):
            self.write('value.hpp', f'#define VALUE {value}\n')
            time.sleep(0.02)
        watcher.cycle(2)
        watcher.quiet()
        self.write('assets/input.txt', 'changed\n')
        watcher.cycle(3)
        watcher.quiet()

    def test_edits_during_child_are_not_lost(self):
        """A slow hook gives an explicit rendezvous for an in-build edit."""
        self.write('pause.py', '''from pathlib import Path
import time
root = Path.cwd()
if (root / 'block').exists():
    print('BLOCKED_BUILD', flush=True)
    deadline = time.monotonic() + 15
    while not (root / 'release').exists():
        if time.monotonic() > deadline:
            raise SystemExit(9)
        time.sleep(0.02)
''')
        self.manifest('[[command]]\nname="pause"\nargv=' +
                      json.dumps([sys.executable, 'pause.py']) + '\nphases=["before-build"]\n')
        watcher = self.watch('--run')
        self.write('block', '')
        self.write('value.hpp', '#define VALUE 2\n')
        watcher.text('BLOCKED_BUILD')
        self.write('value.hpp', '#define VALUE 3\n')
        self.write('release', '')
        watcher.cycle(3)
        self.assertIn('VALUE:3', watcher.output())
        watcher.quiet()

    def test_workspace_and_local_dependency_roots(self):
        """Watch member manifests and local header-only imports outside root."""
        self.write('../sdk/plain.hpp', '#define LOCAL_VALUE 1\n')
        self.write('app/clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n'
                   '[dependencies]\nsdk={path="../../sdk",build_system="header-only"}\n')
        self.write('app/main.cpp', '#include <plain.hpp>\n#include <cstdio>\n'
                   'int main() { printf("LOCAL:%d\\n", LOCAL_VALUE); }\n')
        (self.root / 'main.cpp').unlink()
        self.manifest(project='[workspace]\nmembers=["app"]\n')
        watcher = self.watch('--run', '--target', 'app::app')
        self.assertIn('LOCAL:1', watcher.output())
        self.write('../sdk/plain.hpp', '#define LOCAL_VALUE 2\n')
        watcher.cycle(2)
        self.assertIn('LOCAL:2', watcher.output())
        watcher.quiet()

    def test_test_mode_and_release(self):
        """The normal test selector and independent release directory are reused."""
        self.write('check.cpp', '#include <cstdio>\nint main() { puts("CHECK:1"); }\n')
        self.manifest('[[test]]\nname="check"\nsources=["check.cpp"]\n')
        watcher = self.watch('--test', '--release', '--jobs', '1', '--target', 'check')
        self.assertIn('CHECK:1', watcher.output())
        self.assertTrue((self.root / 'out-release/compile_commands.json').is_file())
        self.write('check.cpp', '#include <cstdio>\nint main() { puts("CHECK:2"); }\n')
        watcher.cycle(2)
        self.assertIn('CHECK:2', watcher.output())
        watcher.quiet()

    def test_option_validation(self):
        """Reject ambiguous modes/arguments and unbounded delays before watching."""
        for arguments in (['--run', '--test'], ['--', 'unexpected'],
                          ['--interval', '0'], ['--debounce', '-1']):
            with self.subTest(arguments=arguments):
                result = subprocess.run([OPTIONS.clutch, 'watch', *arguments], cwd=self.root,
                                        capture_output=True, text=True, timeout=TIMEOUT)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'out-debug').exists())

    @unittest.skipIf(os.name == 'nt', 'POSIX signal status; Windows cleanup sends CTRL_BREAK')
    def test_idle_interrupt_is_prompt(self):
        """A graceful idle shutdown does not launch another build."""
        watcher = self.watch()
        watcher.process.send_signal(signal.SIGINT)
        self.assertEqual(watcher.process.wait(timeout=5), 130)
        watcher.reader.join(timeout=2)
        self.assertIn('Stopped watching', watcher.output())
        self.assertEqual(watcher.cycles(), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        value = getattr(OPTIONS, option)
        setattr(OPTIONS, option, os.path.abspath(shutil.which(value) or value))
    unittest.main(argv=[sys.argv[0], *arguments])
