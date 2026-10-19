#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Publish isolated local Git/catalog examples without changing global Git settings.

Python is only used to prepare these demonstration repositories. Clutch itself
does not require Python, and the other examples need no preparation script.
"""
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / 'Clutch-build-fixtures'
VERSION = '1.0.0'
TAG = 'v' + VERSION
OWNER = 'Clutch dependency example repositories\n'


def git(directory, *arguments):
    """Keep fixture identity/signing policy local to each child invocation."""
    return subprocess.check_output(
        ['git', '-c', 'user.name=Clutch Examples',
         '-c', 'user.email=examples@example.invalid', '-c', 'core.autocrlf=false',
         '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false', *arguments],
        cwd=directory, text=True).strip()


def write_generated(path, text):
    """Preserve user edits and reject redirected generated output paths."""
    if path.is_symlink():
        raise RuntimeError(f'refusing redirected example output: {path}')
    if path.exists():
        if path.read_text(encoding='utf-8') != text:
            raise RuntimeError(f'example output was edited; inspect it before preparing again: {path}')
        return
    path.write_bytes(text.encode('utf-8'))


def publish(repository, source=None, files=None):
    """Reuse clean published fixtures, or create one new local repository."""
    if repository.is_symlink() or repository.resolve() != repository:
        raise RuntimeError(f'refusing redirected example repository: {repository}')
    if repository.exists():
        if not (repository / '.git').is_dir() or git(repository, 'status', '--porcelain'):
            raise RuntimeError(f'example repository is missing Git metadata or has edits: {repository}')
        return git(repository, 'rev-parse', f'{TAG}^{{commit}}')

    if source:
        shutil.copytree(source, repository,
                        ignore=shutil.ignore_patterns('Clutch-build-*', '.clutch', '.git'))
    else:
        repository.mkdir(parents=True)
        for relative, text in (files or {}).items():
            destination = repository / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(text.encode('utf-8'))
    git(repository, 'init', '-q')
    git(repository, 'add', '.')
    git(repository, 'commit', '-qm', 'Publish dependency example')
    git(repository, 'tag', TAG)
    return git(repository, 'rev-parse', 'HEAD')


def main():
    """Generate fixture repositories and substitute an exact catalog baseline."""
    if OUTPUT.is_symlink() or OUTPUT.resolve() != OUTPUT:
        raise RuntimeError(f'refusing redirected example directory: {OUTPUT}')
    marker = OUTPUT / '.example-owner'
    if OUTPUT.exists() and any(OUTPUT.iterdir()) and not marker.is_file():
        raise RuntimeError(f'refusing an unowned example directory: {OUTPUT}')
    OUTPUT.mkdir(exist_ok=True)
    write_generated(marker, OWNER)

    repositories = OUTPUT / 'repositories'
    if repositories.is_symlink():
        raise RuntimeError(f'refusing redirected repositories directory: {repositories}')
    repositories.mkdir(exist_ok=True)
    number = repositories / 'number'
    publish(number, source=ROOT / 'local/number')
    catalog = repositories / 'catalog'
    baseline = publish(catalog, files={
        'ports/demo-number/vcpkg.json': json.dumps(
            {'name': 'demo-number', 'version': VERSION}, indent=2) + '\n',
        'ports/demo-number/portfile.cmake':
            '# Source coordinates only; Clutch does not execute this recipe.\n'
            'vcpkg_from_git(\n    OUT_SOURCE_PATH SOURCE_PATH\n'
            f'    URL "{number.as_posix()}"\n    REF "{TAG}"\n)\n',
    })
    template = (ROOT / 'catalog/clutch.toml.in').read_text(encoding='utf-8')
    write_generated(ROOT / 'catalog/clutch.toml', template.replace('@CATALOG_BASELINE@', baseline))
    print(f'Local Git repository: {number}')
    print(f'Catalog baseline: {baseline}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
