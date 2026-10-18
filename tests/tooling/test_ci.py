#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Keep CI executable discovery faithful to compiler-driver invocation semantics."""

import os
from pathlib import Path
import sys
import tempfile
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
                             [('clutch', 'run'), ('clutch', 'run', '--release')])
            self.assertEqual([command for command in commands if command[1] == 'test'],
                             [('clutch', 'test'), ('clutch', 'test', '--release')])
            self.assertTrue(all('--locked' in command for command in commands if command[1] == '.'))


if __name__ == '__main__':
    unittest.main()
