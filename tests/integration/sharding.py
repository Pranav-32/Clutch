#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Run one deterministic shard of a unittest module when CTest requests it.

CLUTCH_TEST_SHARD=<index>/<count> keeps every test whose position in the
sorted test-ID list has that remainder. Every test belongs to exactly one
shard, so the shards of a suite together run the complete suite. Without the
variable the whole module runs, exactly as before.
"""

import os
import unittest


def _shard():
    value = os.environ.get('CLUTCH_TEST_SHARD', '')
    if not value:
        return None
    index, separator, count = value.partition('/')
    if not separator or not index.isdecimal() or not count.isdecimal():
        raise SystemExit(f'invalid CLUTCH_TEST_SHARD: {value!r} (expected index/count)')
    index, count = int(index), int(count)
    if count < 1 or index >= count:
        raise SystemExit(f'invalid CLUTCH_TEST_SHARD: {value!r} (index must be below count)')
    return index, count


def _flatten(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _flatten(item)
        else:
            yield item


class _ShardLoader(unittest.TestLoader):
    """Select this shard's tests after normal discovery and -k filtering."""

    def __init__(self, index, count):
        super().__init__()
        self._index, self._count = index, count

    def loadTestsFromModule(self, module, *args, **kwargs):
        tests = sorted(_flatten(super().loadTestsFromModule(module, *args, **kwargs)),
                       key=lambda test: test.id())
        return self.suiteClass(test for position, test in enumerate(tests)
                               if position % self._count == self._index)


def main(argv):
    """Drop-in replacement for unittest.main(argv=argv)."""
    shard = _shard()
    if shard is None:
        unittest.main(argv=argv)
    else:
        unittest.main(argv=argv, testLoader=_ShardLoader(*shard))
