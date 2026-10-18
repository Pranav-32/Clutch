#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Reference file-API consumer: query, configure, then read a project snapshot.

Requires Python 3.9+ for this example only. The Clutch executable is native.
No command received from a reply is executed by this reader.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def invoke(arguments):
    """Keep CLI failures visible; never fall back to an older successful model."""
    result = subprocess.run(arguments, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def signature(path):
    """Match the documented FNV-1a byte fingerprint; this is not authentication."""
    fnv1a_offset_basis = 14695981039346656037
    fnv1a_prime = 1099511628211
    hash_bits = 64
    unsigned_64_mask = (1 << hash_bits) - 1
    # Read in 64 KiB blocks while applying FNV-1a to each byte in order.
    read_block_bytes = 64 * 1024
    value = fnv1a_offset_basis
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(read_block_bytes), b''):
            for byte in chunk:
                value = ((value ^ byte) * fnv1a_prime) & unsigned_64_mask
    return {'algorithm': 'fnv1a64', 'value': f'{value:016x}'}


def read_reference(directory, reference):
    """Follow only reply-relative references; never accept absolute/escaping paths."""
    name = reference['jsonFile']
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts or '\\' in name:
        raise RuntimeError('unsafe reply reference')
    path = (directory / relative).resolve()
    if not path.is_relative_to(directory.resolve()):
        raise RuntimeError('reply reference escapes its directory')
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('kind') != reference['kind'] or value.get('version', {}).get('major') != 1:
        raise RuntimeError('unexpected reply object kind/version')
    return value


def read_snapshot(location):
    """Read one immutable generation; an error index supersedes old successes."""
    reply = Path(location['paths']['reply'])
    if (reply.parent / 'configure.lock').exists():
        raise RuntimeError('configuration is active or its lock needs inspection')
    indices = sorted(path for path in reply.glob('index-*.json')
                     if re.fullmatch(r'index-[0-9]{20}\.json', path.name))
    if not indices:
        raise RuntimeError('no file-API reply; create a query and configure first')
    index = json.loads(indices[-1].read_text(encoding='utf-8'))
    if index.get('api', {}).get('major') != 1:
        raise RuntimeError('unsupported file-API layout')
    if index.get('status') != 'success':
        raise RuntimeError('configuration failed: ' + index.get('error', {}).get('message', 'unknown error'))
    if index.get('profile') != location['profile'] or index.get('paths') != {
            'source': location['paths']['source'], 'build': location['paths']['build']}:
        raise RuntimeError('reply belongs to another project/profile')
    if index.get('manifest') != signature(Path(location['paths']['source']) / 'clutch.toml'):
        raise RuntimeError('manifest changed; configure again before reading the model')
    for item in index.get('configurationInputs', []):
        if signature(Path(item['path'])) != item['signature']:
            raise RuntimeError('configuration input changed; configure again: ' + item['path'])
    result = {}
    for reference in index['objects']:
        result[reference['kind']] = read_reference(reply, reference)
    if 'codemodel' not in result:
        raise RuntimeError('codemodel was not requested or its query failed')
    targets = []
    for reference in result['codemodel']['targets']:
        target_reference = {**reference, 'kind': 'target', 'version': {'major': 1, 'minor': 0}}
        targets.append(read_reference(reply, target_reference))
    # A writer can start while old replies are being read. Those replies remain
    # valid, but this consumer retries rather than reporting an older selection.
    if (reply.parent / 'configure.lock').exists() or sorted(
            path.name for path in reply.glob('index-*.json')
            if re.fullmatch(r'index-[0-9]{20}\.json', path.name))[-1] != indices[-1].name:
        raise RuntimeError('configuration changed while reading; retry')
    return {'generation': index['generation'], 'profile': index['profile'],
            'objects': result, 'targets': targets}


def main():
    """Use capabilities and locate instead of guessing an IDE's build directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clutch', default='clutch')
    parser.add_argument('--project', default='.')
    parser.add_argument('--release', action='store_true')
    parser.add_argument('--read-only', action='store_true', help='Read existing replies without configuring')
    parser.add_argument('--cc')
    parser.add_argument('--cxx')
    parser.add_argument('--build-tool')
    args = parser.parse_args()
    capabilities = json.loads(invoke([args.clutch, 'capabilities']))
    if capabilities['fileApi']['version']['major'] != 1:
        raise RuntimeError('Clutch does not support file-API layout v1')
    profile = ['--release'] if args.release else []
    location = json.loads(invoke([args.clutch, 'locate', '--project', args.project, *profile]))
    if not args.read_only:
        query = Path(location['paths']['query']) / 'client-reference/query.json'
        query.parent.mkdir(parents=True, exist_ok=True)
        query.write_text(json.dumps({'requests': [{'kind': kind, 'version': 1}
                         for kind in ('codemodel', 'toolchains', 'inputs')]}, indent=2) + '\n', encoding='utf-8')
        command = [args.clutch, location['paths']['source'], *profile]
        for flag, value in (('--cc', args.cc), ('--cxx', args.cxx), ('--build-tool', args.build_tool)):
            if value:
                command.extend((flag, value))
        invoke(command)
    print(json.dumps(read_snapshot(location), indent=2, ensure_ascii=False))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f'file-API client: {error}', file=sys.stderr)
        sys.exit(1)
