#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Shards of an integration suite must partition it: every test runs once."""

import os
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests/integration'))
import sharding


def fixture_module(count):
    """A throwaway module holding `count` tests split over two classes."""
    module = types.ModuleType('sharding_fixture')
    middle = count // 2
    for class_name, indices in (('Alpha', range(0, middle)), ('Beta', range(middle, count))):
        methods = {f'test_{index:02d}': (lambda self: None) for index in indices}
        setattr(module, class_name, type(class_name, (unittest.TestCase,), methods))
    return module


def selected(module, shard):
    """Test IDs the loader selects for `shard` ("i/n"), or all tests for None."""
    loader = unittest.TestLoader()
    if shard:
        with patch.dict(os.environ, {'CLUTCH_TEST_SHARD': shard}):
            loader = sharding._ShardLoader(*sharding._shard())
    return [test.id() for test in sharding._flatten(loader.loadTestsFromModule(module))]


class ShardingTests(unittest.TestCase):
    def test_shards_partition_the_suite(self):
        module = fixture_module(11)
        everything = sorted(selected(module, None))
        for count in (1, 2, 3, 4, 11, 12):
            with self.subTest(count=count):
                parts = [selected(module, f'{index}/{count}') for index in range(count)]
                combined = [test for part in parts for test in part]
                self.assertEqual(sorted(combined), everything)
                self.assertEqual(len(combined), len(set(combined)))
                sizes = [len(part) for part in parts]
                self.assertLessEqual(max(sizes) - min(sizes), 1)

    def test_invalid_shard_values_are_rejected(self):
        for value in ('1', '2/2', '-1/2', 'a/b', '0/0', '1/x'):
            with self.subTest(value=value), patch.dict(os.environ, {'CLUTCH_TEST_SHARD': value}):
                with self.assertRaises(SystemExit):
                    sharding._shard()

    def test_unset_variable_runs_everything(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(sharding._shard())


if __name__ == '__main__':
    unittest.main()
