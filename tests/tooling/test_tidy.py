#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Keep static-analysis coverage honest without requiring LLVM in tooling tests."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/tidy.py'
SPEC = importlib.util.spec_from_file_location('clutch_tidy', SCRIPT)
TIDY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIDY)


class TidyCoverageTests(unittest.TestCase):
    """Missing sources must fail rather than silently reducing verification."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='clutch tidy ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        (self.root / 'src').mkdir()
        self.source = self.root / 'src/main.cpp'
        self.source.write_text('int main() {}\n', encoding='utf-8')
        self.database = self.root / 'compile_commands.json'

    def write_database(self, records):
        """Produce the same JSON record shape emitted by CMake and Clutch."""
        self.database.write_text(json.dumps(records), encoding='utf-8')

    def record(self, source):
        return {'directory': str(self.root), 'file': str(source),
                'arguments': ['clang++', '-c', str(source)]}

    def test_inventory_prunes_generated_and_vendor_sources(self):
        """Generated example outputs are never treated as owned input files."""
        for folder in ('examples/demo', 'examples/demo/Clutch-build-debug',
                       'examples/demo/.clutch', 'examples/demo/third_party'):
            path = self.root / folder
            path.mkdir(parents=True, exist_ok=True)
            (path / 'unit.cpp').write_text('', encoding='utf-8')
        actual = TIDY.source_inventory(self.root, ('src', 'examples'))
        self.assertEqual(actual, {self.source, self.root / 'examples/demo/unit.cpp'})

    def test_missing_source_prevents_partial_success(self):
        self.write_database([])
        with self.assertRaisesRegex(ValueError, 'no compilation command'):
            TIDY.analysis_jobs([self.database], {self.source})

    def test_duplicate_commands_and_unowned_sources(self):
        """Repeated graph support objects do not create redundant analysis jobs."""
        self.write_database([self.record(self.source), self.record(self.source),
                             self.record(self.root / 'generated.cpp')])
        self.assertEqual(TIDY.analysis_jobs([self.root], {self.source}),
                         [(self.root, self.source)])

    def test_multiple_databases_cover_independent_scopes(self):
        example = self.root / 'example.cpp'
        example.write_text('int example();\n', encoding='utf-8')
        self.write_database([self.record(self.source)])
        second = self.root / 'example-database.json'
        second.write_text(json.dumps([self.record(example)]), encoding='utf-8')
        self.assertEqual(len(TIDY.analysis_jobs([self.database, second],
                                              {self.source, example})), 2)

    def test_invalid_records_and_external_scope_are_rejected(self):
        for records in ({}, [42], [{'file': str(self.source)}]):
            with self.subTest(records=records):
                self.write_database(records)
                with self.assertRaises(ValueError):
                    TIDY.analysis_jobs([self.database], {self.source})
        with self.assertRaisesRegex(ValueError, 'inside the checkout'):
            TIDY.source_inventory(self.root, ('..',))


if __name__ == '__main__':
    unittest.main()
