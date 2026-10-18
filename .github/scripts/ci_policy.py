#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Select verification by changed contents, never by PR titles or branch names."""

import argparse
import json
import os
import re
import subprocess

from release import ROOT, read_version

# These files change release identity and notes, without changing build behavior.
# Any source, workflow, example, schema or test edit requires full native CI.
RELEASE_METADATA = frozenset(('VERSION', '.github/release-please-manifest.json',
                              'docs/CHANGELOG.md'))
# Git supplies opaque object IDs: SHA-1 has 40 hex digits, SHA-256 has 64.
GIT_OBJECT_ID_PATTERN = re.compile(r'(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})')


def metadata_only(base, root=ROOT):
    """Require a nonempty diff containing only the exact release-file allowlist."""
    # Manual runs, initial pushes and unavailable history receive full coverage.
    if not GIT_OBJECT_ID_PATTERN.fullmatch(base or '') or not int(base, 16):
        return False
    try:
        changed = subprocess.check_output(
            ['git', 'diff', '--name-only', '--no-renames', '-z', base, 'HEAD', '--'],
            cwd=root, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError:
        return False
    # Disabling rename detection exposes both sides of a move; a source moved
    # into an allowed metadata filename must never opt out of native tests.
    paths = {os.fsdecode(path) for path in changed.split(b'\0') if path}
    return bool(paths) and paths <= RELEASE_METADATA


def validate_metadata(root=ROOT):
    """Require canonical, matching release versions and a changelog entry."""
    version = read_version(root)
    manifest = json.loads((root / '.github/release-please-manifest.json').read_text(encoding='utf-8'))
    if not isinstance(manifest, dict) or manifest.get('.') != version:
        raise ValueError('release-please manifest version must match VERSION')
    changelog = (root / 'docs/CHANGELOG.md').read_text(encoding='utf-8')
    if not re.search(rf'^## (?:\[{re.escape(version)}\]|{re.escape(version)})(?:\s|\(|$)',
                     changelog, re.MULTILINE):
        raise ValueError(f'changelog lacks the release entry for {version}')
    return version


def main():
    """Publish a small workflow decision or validate metadata without compiling."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('plan', 'validate'))
    args = parser.parse_args()
    if args.command == 'validate':
        print(f'Release metadata validated: {validate_metadata()}')
        return
    only_metadata = metadata_only(os.environ.get('CI_BASE_SHA', ''))
    print('CI scope: ' + ('release metadata' if only_metadata else 'full native verification'))
    if output := os.environ.get('GITHUB_OUTPUT'):
        with open(output, 'a', encoding='utf-8') as stream:
            stream.write(f'metadata_only={str(only_metadata).lower()}\n')


if __name__ == '__main__':
    main()
