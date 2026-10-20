#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Keep CI executable discovery faithful to compiler-driver invocation semantics."""

from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '.github/scripts'))
import ci


class ToolDiscoveryTests(unittest.TestCase):
    """Use disposable files; compiler-backed coverage remains in the native job."""

    def test_direct_executable_path(self):
        with tempfile.TemporaryDirectory(prefix='clutch tool ') as temporary:
            executable = Path(temporary) / 'compiler'
            executable.touch()
            with patch.object(ci.shutil, 'which', return_value=str(executable)):
                self.assertEqual(ci.require_tool('compiler'), os.path.abspath(executable))

    @unittest.skipUnless(os.name == 'posix', 'POSIX compiler-driver symlink regression')
    def test_compiler_symlink_names_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix='clutch drivers ') as temporary:
            directory = Path(temporary)
            executable = directory / 'clang'
            executable.touch()
            for name in ('clang++', 'clang-cl', 'c++'):
                with self.subTest(name=name):
                    alias = directory / name
                    alias.symlink_to(executable.name)
                    with patch.object(ci.shutil, 'which', return_value=str(alias)):
                        result = Path(ci.require_tool(name))
                    self.assertEqual(result.name, name)
                    self.assertEqual(result, alias.absolute())
                    self.assertEqual(result.resolve(), executable.resolve())

    def test_missing_executable_reports_requested_tool(self):
        with patch.object(ci.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'required tool not found: missing-compiler'):
                ci.require_tool('missing-compiler')


class WorkflowExecutionTests(unittest.TestCase):
    """Bound concurrency and keep examples exercising their public workflows."""

    def test_parallelism_is_bounded_and_validated(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(ci.job_count('CLUTCH_TEST_JOBS'), str(ci.DEFAULT_JOB_COUNT))
        for value in ('0', '-1', 'unlimited', '', '2 --extra', '\u0662'):
            with self.subTest(value=value), patch.dict(os.environ, {'CLUTCH_TEST_JOBS': value}):
                with self.assertRaisesRegex(ValueError, 'positive integer'):
                    ci.job_count('CLUTCH_TEST_JOBS')

    def test_examples_build_through_run_and_keep_profiles_and_locks(self):
        with tempfile.TemporaryDirectory(prefix='clutch example policy ') as temporary:
            root = Path(temporary)
            example = root / 'examples/probe'
            example.mkdir(parents=True)
            (example / 'clutch.toml').write_text('[project]\nname="probe"\n[[test]]\nname="check"\n',
                                                encoding='utf-8')
            (example / 'clutch.lock').write_text('fixture lock', encoding='utf-8')
            with patch.object(ci, 'ROOT', root), patch.object(ci, 'run') as run:
                ci.test_examples('clutch', 'cc', 'c++', 'xcode')
            commands = [call.args for call in run.call_args_list]
            self.assertFalse(any(command[1] == 'build' for command in commands))
            self.assertEqual([command for command in commands if command[1] == 'run'],
                             [('clutch', 'run', '--jobs', '1'),
                              ('clutch', 'run', '--release', '--jobs', '1')])
            self.assertEqual([command for command in commands if command[1] == 'test'],
                             [('clutch', 'test', '--jobs', '1'),
                              ('clutch', 'test', '--release', '--jobs', '1')])
            self.assertTrue(all('--locked' in command for command in commands if command[1] == '.'))

    def test_examples_overlap_with_bounded_workers_and_keep_failure_logs(self):
        with tempfile.TemporaryDirectory(prefix='clutch parallel examples ') as temporary:
            root = Path(temporary)
            names = ('a', 'b', 'c', 'd')
            for name in names:
                manifest = root / 'examples' / name / 'clutch.toml'
                manifest.parent.mkdir(parents=True)
                manifest.touch()
            barrier = threading.Barrier(2, timeout=10)
            lock = threading.Lock()
            completed = []
            running = peak = 0

            def check(manifest, work, binary, cc, cxx, generator, output):
                nonlocal running, peak
                with lock:
                    running += 1
                    peak = max(peak, running)
                # Both slots must be active; no wall-clock sleeps or thresholds.
                barrier.wait()
                with lock:
                    running -= 1
                    completed.append(manifest.parent.name)
                print('fixture output: ' + manifest.parent.name, file=output)
                if manifest.parent.name == 'a':
                    raise RuntimeError('fixture failed')

            captured = io.StringIO()
            with patch.object(ci, 'ROOT', root), patch.object(ci, 'test_example', check), \
                    patch.dict(os.environ, {'CLUTCH_TEST_JOBS': '2'}), redirect_stdout(captured):
                with self.assertRaisesRegex(RuntimeError, 'fixture failed'):
                    ci.test_examples('clutch', 'cc', 'c++', 'xcode')
            self.assertEqual(peak, 2)
            self.assertCountEqual(completed, names)
            for name in names:
                self.assertIn('fixture output: ' + name, captured.getvalue())


if __name__ == '__main__':
    unittest.main()
