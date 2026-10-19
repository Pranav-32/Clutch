#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Validate release identities and package installed native binaries.

Python is development/CI tooling only; installed Clutch has no Python dependency.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Each supported release target has a matching build-matrix job.
TARGETS = (
    'linux-x86_64-gcc',
    'linux-x86_64-clang',
    'windows-x86_64-msvc',
    'macos-arm64-apple-clang',
    'macos-x86_64-apple-clang',
)
NOTICES = ('LICENSE', 'CLI11.LICENSE', 'tomlplusplus.LICENSE', 'nlohmann.LICENSE')
# One MiB balances streaming checksum throughput against temporary memory use.
CHECKSUM_BLOCK_BYTES = 1024 * 1024
NUMBER = r'(?:0|[1-9][0-9]*)'
IDENTIFIER = r'(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
VERSION_PATTERN = re.compile(
    rf'{NUMBER}\.{NUMBER}\.{NUMBER}(?:-{IDENTIFIER}(?:\.{IDENTIFIER})*)?'
)


def read_version(root=ROOT):
    """Reject ambiguous versions before generating filenames or Git tags."""
    version = (root / 'VERSION').read_text(encoding='utf-8').strip()
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError('VERSION must be canonical major.minor.patch[-prerelease]')
    return version


def validate_tag(tag, root=ROOT):
    """Require the release tag to name VERSION and point at this checkout."""
    version = read_version(root)
    if tag != f'v{version}':
        raise ValueError(f'tag must be v{version}, received {tag!r}')
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    tagged = subprocess.check_output(
        ['git', 'rev-parse', '--verify', f'refs/tags/{tag}^{{commit}}'], cwd=root, text=True
    ).strip()
    if tagged != head:
        raise ValueError('release tag no longer points at the checked-out commit')
    return head


def archive_name(version, target):
    """Keep package naming identical for builders and release verification."""
    if target not in TARGETS:
        raise ValueError(f'unsupported release target: {target}')
    suffix = '.zip' if target.startswith('windows-') else '.tar.gz'
    return f'clutch-{version}-{target}{suffix}'


def checksum(path):
    """Stream archive bytes so packaging does not require archive-sized memory."""
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(CHECKSUM_BLOCK_BYTES), b''):
            digest.update(block)
    return digest.hexdigest()


def package(stage, output, target, version):
    """Archive CMake's install tree with all mandatory license notices."""
    binary = stage / 'bin' / ('clutch.exe' if target.startswith('windows-') else 'clutch')
    required = [binary, *(stage / 'share/licenses/clutch' / name for name in NOTICES)]
    if any(not path.is_file() for path in required):
        raise ValueError('install tree lacks the executable or required license notices')
    actual = subprocess.check_output([str(binary), '--version'], text=True).strip()
    if actual != f'clutch {version}':
        raise ValueError(f'installed executable has unexpected version: {actual!r}')
    output.mkdir(parents=True, exist_ok=True)
    archive = output / archive_name(version, target)
    prefix = f'clutch-{version}'
    files = sorted(path for path in stage.rglob('*') if path.is_file())
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
            for path in files:
                bundle.write(path, f'{prefix}/{path.relative_to(stage).as_posix()}')
    else:
        with tarfile.open(archive, 'w:gz') as bundle:
            for path in files:
                bundle.add(path, arcname=f'{prefix}/{path.relative_to(stage).as_posix()}', recursive=False)
    archive.with_name(archive.name + '.sha256').write_text(
        f'{checksum(archive)}  {archive.name}\n', encoding='utf-8'
    )
    return archive


def verify_assets(directory, version):
    """Require every platform archive and verify its builder-produced digest."""
    archives = [directory / archive_name(version, target) for target in TARGETS]
    expected = {path.name for archive in archives for path in (archive, archive.with_name(archive.name + '.sha256'))}
    # Recompute our own summary on retries; builder inputs must still match.
    actual = {path.name for path in directory.iterdir() if path.name != 'SHA256SUMS'}
    if actual != expected:
        raise ValueError(f'release assets differ: missing={expected - actual}, unexpected={actual - expected}')
    lines = []
    for archive in archives:
        line = f'{checksum(archive)}  {archive.name}\n'
        if archive.with_name(archive.name + '.sha256').read_text(encoding='utf-8') != line:
            raise ValueError(f'checksum mismatch: {archive.name}')
        lines.append(line)
    (directory / 'SHA256SUMS').write_text(''.join(lines), encoding='utf-8')


def publish(directory, tag):
    """Upload verified artifacts to a draft, then publish the complete release.

    A retry may resume a draft. Published releases are immutable to this script.
    GH_TOKEN and GH_REPO are supplied only by the publishing workflow job.
    """
    version = read_version()
    validate_tag(tag)
    verify_assets(directory, version)
    view = subprocess.run(
        ['gh', 'release', 'view', tag, '--json', 'isDraft,assets'], text=True, capture_output=True
    )
    files = sorted(path for path in directory.iterdir() if path.is_file())
    if view.returncode == 0:
        existing = json.loads(view.stdout)
        if not existing['isDraft']:
            raise ValueError('release is already published; create a new version')
        if {asset['name'] for asset in existing['assets']} - {path.name for path in files}:
            raise ValueError('existing draft contains unexpected assets; review it manually')
    else:
        # A permissions/network failure also fails create; never proceed to upload
        # unless a valid draft exists or was successfully created.
        command = ['gh', 'release', 'create', tag, '--verify-tag', '--draft', '--generate-notes',
                   '--title', f'Clutch {version}']
        if '-' in version:
            command.append('--prerelease')
        subprocess.run(command, check=True)
    subprocess.run(['gh', 'release', 'upload', tag, *(str(path) for path in files), '--clobber'], check=True)
    uploaded = json.loads(subprocess.check_output(
        ['gh', 'release', 'view', tag, '--json', 'assets'], text=True
    ))
    if {asset['name'] for asset in uploaded['assets']} != {path.name for path in files}:
        raise ValueError('uploaded assets are incomplete; release remains a draft')
    validate_tag(tag)
    subprocess.run(['gh', 'release', 'edit', tag, '--draft=false',
                    f'--prerelease={str("-" in version).lower()}'], check=True)


def main():
    """Expose validation and publishing without importing CI build machinery."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('validate', 'publish'))
    parser.add_argument('--tag', required=True)
    parser.add_argument('--artifacts', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    if args.command == 'validate':
        commit = validate_tag(args.tag)
        if output := os.environ.get('GITHUB_OUTPUT'):
            with open(output, 'a', encoding='utf-8') as stream:
                stream.write(f'commit={commit}\n')
    else:
        publish(args.artifacts.resolve(), args.tag)


if __name__ == '__main__':
    main()
