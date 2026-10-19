#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build, test, install and package one native CI target using argument arrays."""

import argparse
import json
import os
import platform
import shutil
import subprocess
import tempfile
import tomllib
from pathlib import Path

from release import ROOT, TARGETS, package

# Allow slow hosted runners five minutes for each compiler-backed CTest suite.
TEST_TIMEOUT_SECONDS = 300
# Limit nested compiler-backed suites rather than starting one per CPU core.
DEFAULT_JOB_COUNT = 2


def run(*command, cwd=ROOT):
    """Log exact argument boundaries and retain native failure exit status."""
    print(f'Running: {command!r}', flush=True)
    subprocess.run([str(argument) for argument in command], cwd=cwd, check=True)


def require_tool(name):
    """Locate an executable while preserving its language-selecting driver name."""
    if resolved := shutil.which(name):
        # clang++ and clang-cl may be symlinks to clang. Dereferencing them changes
        # argv[0] and can disable C++ runtime linking or Microsoft-style options.
        return os.path.abspath(resolved)
    raise RuntimeError(f'required tool not found: {name}')


def job_count(variable):
    """Reject unbounded/invalid parallelism before starting any build work."""
    value = os.environ.get(variable, str(DEFAULT_JOB_COUNT))
    if not value.isascii() or not value.isdecimal() or int(value) < 1:
        raise ValueError(f'{variable} must be a positive integer')
    return value


def test_examples(binary, cc, cxx, generator):
    """Exercise shipped projects in both profiles without polluting examples/."""
    with tempfile.TemporaryDirectory(prefix='clutch examples ') as temporary:
        # Resolve macOS /var aliases before Clutch's build ownership checks.
        work = Path(temporary).resolve()
        for manifest in sorted((ROOT / 'examples').glob('*/clutch.toml')):
            project = work / manifest.parent.name
            settings = tomllib.loads(manifest.read_text(encoding='utf-8'))
            shutil.copytree(manifest.parent, project,
                            ignore=shutil.ignore_patterns('Clutch-build-*', '.clutch'))
            # Checked-in resolution must already match; CI must not silently
            # repair an outdated example lock before testing it.
            frozen = ('--locked',) if (project / 'clutch.lock').is_file() else ()
            for profile in ((), ('--release',)):
                run(binary, '.', '--generator', generator, '--cc', cc, '--cxx', cxx,
                    *profile, *frozen, cwd=project)
                # run builds its executable first; another build here starts
                # Xcode/MSBuild twice without adding example coverage.
                run(binary, 'run', *profile, cwd=project)
                run(binary, '.', '--locked', '--generator', generator, '--cc', cc, '--cxx', cxx,
                    *profile, cwd=project)
                if settings.get('test') or settings.get('workspace'):
                    run(binary, 'test', *profile, cwd=project)
                if manifest.parent.name == 'multi-file':
                    run(binary, 'run', *profile, '--', 'Clutch contributor', cwd=project)


def build(target, build_dir, output):
    """Require host-specific integration suites before producing a release asset."""
    build_jobs, test_jobs = job_count('CLUTCH_BUILD_JOBS'), job_count('CLUTCH_TEST_JOBS')
    windows = target.startswith('windows-')
    expected_system = 'Windows' if windows else ('Darwin' if target.startswith('macos-') else 'Linux')
    expected_machine = 'arm64' if '-arm64-' in target else 'x86_64'
    machine = platform.machine().lower().replace('amd64', 'x86_64').replace('aarch64', 'arm64')
    if platform.system() != expected_system or machine != expected_machine:
        raise ValueError(f'{target} does not match host {platform.system()}/{machine}')
    cc = require_tool(os.environ.get('CC', 'cl' if windows else 'cc'))
    cxx = require_tool(os.environ.get('CXX', 'cl' if windows else 'c++'))
    ninja = require_tool('ninja')
    command = ['cmake', '-S', ROOT, '-B', build_dir, '-G', 'Ninja',
               '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
               f'-DCMAKE_CXX_COMPILER={cxx}', f'-DCMAKE_MAKE_PROGRAM={ninja}']
    # Cache only compilation, never a saved CMake tree, test result or package.
    # The actual compiler paths remain unchanged for Clutch integration tests.
    launcher = os.environ.get('CLUTCH_COMPILER_LAUNCHER')
    command.append(f'-DCMAKE_CXX_COMPILER_LAUNCHER={require_tool(launcher) if launcher else ""}')
    required = {'clutch.help', 'clutch.version', 'clutch.files', 'clutch.graph', 'clutch.file-api', 'clutch.targets',
                'clutch.workspace', 'clutch.dependencies', 'clutch.foreign-dependencies',
                'clutch.package-examples', 'clutch.version-ranges',
                'clutch.source-files', 'clutch.capture', 'clutch.stress-source-files',
                'clutch.stress-capture', 'clutch.stress-integration'}
    if windows:
        command.append(f'-DCLUTCH_TEST_NINJA={ninja}')
        required.update(('clutch.standalone', 'clutch.integration'))
    else:
        command.extend((f'-DCLUTCH_TEST_CC={cc}',
                        f'-DCLUTCH_TEST_MAKE={require_tool("gmake" if platform.system() == "Darwin" else "make")}'))
        required.add('clutch.posix-integration')
    run(*command)
    run('cmake', '--build', build_dir, '--parallel', build_jobs)
    registered = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(build_dir), '--show-only=json-v1'], text=True
    ))
    missing = required - {test['name'] for test in registered['tests']}
    if missing:
        raise RuntimeError(f'required CTest suites were not registered: {missing}')
    run('ctest', '--test-dir', build_dir, '--output-on-failure',
        '--parallel', test_jobs, '--timeout', str(TEST_TIMEOUT_SECONDS))
    binary = build_dir / ('clutch.exe' if windows else 'clutch')
    if windows:
        run('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            ROOT / 'tests/integration/native-generators.ps1', '-Clutch', binary)
    test_examples(binary, cc, cxx, 'ninja')
    if platform.system() == 'Darwin':
        test_examples(binary, cc, cxx, 'xcode')
    # A temporary install tree prevents stale files from entering an archive.
    with tempfile.TemporaryDirectory(prefix='clutch install ') as temporary:
        stage = Path(temporary).resolve()
        run('cmake', '--install', build_dir, '--prefix', stage)
        version = (build_dir / 'generated/version.txt').read_text(encoding='utf-8').strip()
        package(stage, output, target, version)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, choices=TARGETS)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-ci')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    build(args.target, args.build_dir.resolve(), args.output.resolve())
