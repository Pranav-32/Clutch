#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Analyze owned translation units and reject incomplete compilation databases.

Supply each host-native database with --database. The default inventory includes
src/, tests/unit/ and examples/. Headers are checked through including units;
inactive platform branches still need analysis on their native host.
Python and clang-tidy are contributor tools, not Clutch runtime dependencies.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = frozenset(('.c', '.cc', '.cpp', '.cxx'))
DEFAULT_SCOPES = ('src', 'tests/unit', 'examples')


def source_inventory(root, scopes):
    """List physical owned sources while pruning generated example build trees."""
    sources = set()
    for scope in scopes:
        directory = (root / scope).resolve()
        if not directory.is_relative_to(root) or not directory.is_dir():
            raise ValueError(f'analysis scope must be a directory inside the checkout: {scope}')
        for folder, children, files in os.walk(directory, followlinks=False):
            children[:] = [name for name in children
                           if not name.startswith(('Clutch-build-', 'build-', 'cmake-build-'))
                           and name not in ('.clutch', '.git', 'third_party')
                           and not (Path(folder) / name).is_symlink()]
            for name in files:
                path = Path(folder) / name
                if path.suffix in SOURCE_SUFFIXES and not path.is_symlink():
                    sources.add(path.resolve())
    return sources


def analysis_jobs(databases, sources):
    """Require every source to have a command before starting any analysis.

    Analyze each database/source pair: different profiles or targets can select
    different preprocessor branches. Third-party and generated units are ignored.
    """
    jobs = set()
    covered = set()
    for location in databases:
        database = location.resolve()
        if database.is_dir():
            database /= 'compile_commands.json'
        records = json.loads(database.read_text(encoding='utf-8-sig'))
        if not isinstance(records, list):
            raise ValueError(f'compilation database must contain an array: {database}')
        for record in records:
            if (not isinstance(record, dict)
                    or not isinstance(record.get('directory'), str)
                    or not isinstance(record.get('file'), str)):
                raise ValueError(f'invalid compilation database record: {database}')
            source = (Path(record['directory']) / record['file']).resolve()
            if source in sources:
                if not record.get('arguments') and not record.get('command'):
                    raise ValueError(f'compilation command is missing: {source}')
                covered.add(source)
                jobs.add((database.parent, source))
    if missing := sources - covered:
        raise ValueError('no compilation command for:\n' + '\n'.join(
            str(path) for path in sorted(missing)))
    if not jobs:
        raise ValueError('no owned translation units selected')
    return sorted(jobs)


def analyze(executable, database, source):
    """Use actual compile settings, fail on diagnostics, and never apply fixes."""
    result = subprocess.run(
        [executable, '--quiet', '--warnings-as-errors=*', '-p', str(database), str(source)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding='utf-8', errors='replace')
    return source, result.returncode, result.stdout


def main():
    """Validate coverage/configuration, then report every analyzed source."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', action='append', type=Path, required=True)
    parser.add_argument('--scope', action='append', help='Checkout-relative source directory; repeatable')
    parser.add_argument('--clang-tidy', default='clang-tidy')
    parser.add_argument('--jobs', type=int, default=1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    sources = source_inventory(ROOT, args.scope or DEFAULT_SCOPES)
    jobs = analysis_jobs(args.database, sources)
    subprocess.run([args.clang_tidy, '--verify-config', '--config-file=' + str(ROOT / '.clang-tidy')],
                   check=True)
    print(f'Coverage: {len(sources)} owned sources, {len(jobs)} database/source pairs', flush=True)
    failed = False
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = pool.map(lambda job: analyze(args.clang_tidy, *job), jobs)
        for source, returncode, output in results:
            print(f'{"FAIL" if returncode else "PASS"}: {source.relative_to(ROOT)}', flush=True)
            if output:
                print(output, flush=True)
            failed = failed or returncode != 0
    return int(failed)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Static analysis failed: {error}', file=sys.stderr)
        sys.exit(1)
