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

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.github/scripts'))
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


if __name__ == '__main__':
    unittest.main()
