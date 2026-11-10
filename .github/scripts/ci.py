#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build, test, install and package one native CI target using argument arrays."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
from pathlib import Path

from release import INSTALLER_TARGETS, ROOT, TARGETS, build_installer, package

# Allow slow hosted runners five minutes for each compiler-backed CTest suite.
TEST_TIMEOUT_SECONDS = 300
# Default to the runner's cores, capped: compiler-backed suites start their own
# processes, so more workers than cores mostly adds contention. Hosted Linux,
# Windows and Intel macOS runners have 4 cores; Apple silicon runners have 3.
MAXIMUM_DEFAULT_JOBS = 4


def default_job_count():
    """Use every runner core up to the cap; at least one job."""
    return max(1, min(os.cpu_count() or 1, MAXIMUM_DEFAULT_JOBS))


def run(*command, cwd=ROOT, output=None):
    """Log exact argument boundaries and retain native failure exit status."""
    print(f'Running: {command!r}', file=output, flush=True)
    started = time.monotonic()
    try:
        subprocess.run([str(argument) for argument in command], cwd=cwd, check=True,
                       stdout=output, stderr=subprocess.STDOUT if output else None)
    finally:
        print(f'Elapsed: {time.monotonic() - started:.2f}s', file=output, flush=True)


def require_tool(name):
    """Locate an executable while preserving its language-selecting driver name."""
    if resolved := shutil.which(name):
        # clang++ and clang-cl may be symlinks to clang. Dereferencing them changes
        # argv[0] and can disable C++ runtime linking or Microsoft-style options.
        return os.path.abspath(resolved)
    raise RuntimeError(f'required tool not found: {name}')


def compiler_launcher():
    """Return the configured compiler cache, or '' when its server cannot start.

    sccache checks its storage when the server starts; a storage outage (for
    example GitHub cache egress limits) then fails every wrapped compile.
    Starting the server first turns that outage into an uncached build.
    """
    launcher = os.environ.get('CLUTCH_COMPILER_LAUNCHER')
    if not launcher:
        return ''
    executable = require_tool(launcher)
    if Path(executable).stem.lower() != 'sccache':
        return executable
    started = subprocess.run([executable, '--start-server'], capture_output=True, text=True)
    if started.returncode != 0:
        reason = (started.stderr or started.stdout).strip().splitlines()
        print(f'::warning::compiler cache unavailable, building without it: '
              f'{reason[0] if reason else f"exit {started.returncode}"}', flush=True)
        return ''
    return executable


def job_count(variable):
    """Reject unbounded/invalid parallelism before starting any build work."""
    value = os.environ.get(variable, str(default_job_count()))
    if not value.isascii() or not value.isdecimal() or int(value) < 1:
        raise ValueError(f'{variable} must be a positive integer')
    return value


def test_example(manifest, work, binary, cc, cxx, generator, output):
    """Keep each project's profiles sequential and its compiler budget at one."""
    project = work / manifest.parent.name
    settings = tomllib.loads(manifest.read_text(encoding='utf-8'))
    shutil.copytree(manifest.parent, project,
                    ignore=shutil.ignore_patterns('Clutch-build-*', '.clutch'))
    # Script examples declare an interpreter explicitly. Use the Python running
    # CI in the disposable copy; Windows need not provide a python3 alias.
    if any(command.get('argv', [])[:1] == ['python3']
           for command in settings.get('command', [])):
        copied_manifest = project / 'clutch.toml'
        content = copied_manifest.read_text(encoding='utf-8')
        content = re.sub(r'(?m)^(\s*argv\s*=\s*\[\s*)"python3"(?=\s*[,\]])',
                         lambda match: match[1] + json.dumps(sys.executable), content)
        copied_manifest.write_text(content, encoding='utf-8')
    # Checked-in resolution must already match; CI must not silently repair it.
    frozen = ('--locked',) if (project / 'clutch.lock').is_file() else ()
    for profile in ((), ('--release',)):
        run(binary, '.', '--generator', generator, '--cc', cc, '--cxx', cxx,
            *profile, *frozen, cwd=project, output=output)
        # run builds first; another build would start Xcode/MSBuild twice.
        run(binary, 'run', *profile, '--jobs', '1', cwd=project, output=output)
        run(binary, '.', '--locked', '--generator', generator, '--cc', cc, '--cxx', cxx,
            *profile, cwd=project, output=output)
        if settings.get('test') or settings.get('workspace'):
            run(binary, 'test', *profile, '--jobs', '1', cwd=project, output=output)
        if manifest.parent.name == 'multi-file':
            run(binary, 'run', *profile, '--jobs', '1', '--', 'Clutch contributor',
                cwd=project, output=output)


def test_examples(binary, cc, cxx, generator):
    """Run independent projects concurrently; retain complete, separate logs."""
    workers = int(job_count('CLUTCH_TEST_JOBS'))
    with tempfile.TemporaryDirectory(prefix='clutch examples ') as temporary:
        # Resolve macOS /var aliases before Clutch's build ownership checks.
        work = Path(temporary).resolve()

        def check(manifest):
            log = work / (manifest.parent.name + '.log')
            with log.open('w', encoding='utf-8') as output:
                test_example(manifest, work, binary, cc, cxx, generator, output)

        failures = []
        started = time.monotonic()
        with ThreadPoolExecutor(max_workers=workers) as executor:
            pending = {executor.submit(check, manifest): manifest.parent.name
                       for manifest in sorted((ROOT / 'examples').glob('*/clutch.toml'))}
            for future in as_completed(pending):
                name = pending[future]
                try:
                    future.result()
                except Exception as error:
                    # Complete other fixtures and print every failure's log.
                    failures.append(error)
                finally:
                    log = work / (name + '.log')
                    print(f'Example: {name} ({generator})', flush=True)
                    if log.exists():
                        print(log.read_text(encoding='utf-8', errors='replace'), flush=True)
        print(f'Examples ({generator}): {time.monotonic() - started:.2f}s; '
              f'{workers} workers', flush=True)
        if failures:
            raise failures[0]


# Hosted CI runs the two halves of a target as concurrent jobs; local runs use
# 'all'. Every stage builds Clutch itself from the same sources.
STAGES = ('all', 'tests', 'examples')


def build(target, build_dir, output, stage='all'):
    """Require host-specific integration suites before producing a release asset.

    'tests' runs CTest and produces the release package; 'examples' builds
    only the executable and runs shipped examples and native generators.
    """
    if stage not in STAGES:
        raise ValueError(f'unknown CI stage: {stage}')
    tests, examples = stage in ('all', 'tests'), stage in ('all', 'examples')
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
               '-DCMAKE_BUILD_TYPE=Release', f'-DBUILD_TESTING={"ON" if tests else "OFF"}',
               f'-DCMAKE_CXX_COMPILER={cxx}', f'-DCMAKE_MAKE_PROGRAM={ninja}']
    # Cache only compilation, never a saved CMake tree, test result or package.
    # The actual compiler paths remain unchanged for Clutch integration tests.
    command.append(f'-DCMAKE_CXX_COMPILER_LAUNCHER={compiler_launcher()}')
    required = {'clutch.help', 'clutch.version', 'clutch.files', 'clutch.graph', 'clutch.file-api', 'clutch.targets',
                'clutch.workspace', 'clutch.dependencies', 'clutch.foreign-dependencies', 'clutch.cmake-projects', 'clutch.git-submodules',
                'clutch.package-examples', 'clutch.version-ranges', 'clutch.bundle', 'clutch.zip',
                'clutch.configured-files', 'clutch.tooling-safety', 'clutch.commands',
                'clutch.watch', 'clutch.feature-examples', 'clutch.defaults',
                'clutch.source-files', 'clutch.capture', 'clutch.tool-discovery', 'clutch.stress-source-files',
                'clutch.stress-capture', 'clutch.stress-integration'}
    if windows:
        command.append(f'-DCLUTCH_TEST_NINJA={ninja}')
        required.update(('clutch.standalone', 'clutch.integration'))
    else:
        command.extend((f'-DCLUTCH_TEST_CC={cc}',
                        f'-DCLUTCH_TEST_MAKE={require_tool("gmake" if platform.system() == "Darwin" else "make")}'))
        required.add('clutch.posix-integration')
    run(*command)
    run('cmake', '--build', build_dir, '--parallel', build_jobs,
        *(() if tests else ('--target', 'clutch')))
    binary = build_dir / ('clutch.exe' if windows else 'clutch')
    if examples:
        if windows:
            run('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                ROOT / 'tests/integration/native-generators.ps1', '-Clutch', binary)
        test_examples(binary, cc, cxx, 'ninja')
        if platform.system() == 'Darwin':
            test_examples(binary, cc, cxx, 'xcode')
    if not tests:
        return

    registered = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(build_dir), '--show-only=json-v1'], text=True
    ))
    names = {test['name'] for test in registered['tests']}
    # Long suites register as shards <suite>.<index>; any shard proves the
    # suite's registration, and CTest runs all of them.
    missing = {suite for suite in required
               if suite not in names
               and not any(name.startswith(suite + '.') and name[len(suite) + 1:].isdecimal()
                           for name in names)}
    if missing:
        raise RuntimeError(f'required CTest suites were not registered: {missing}')
    run('ctest', '--test-dir', build_dir, '--output-on-failure',
        '--parallel', test_jobs, '--timeout', str(TEST_TIMEOUT_SECONDS))
    # A temporary install tree prevents stale files from entering an archive.
    with tempfile.TemporaryDirectory(prefix='clutch install ') as temporary:
        stage = Path(temporary).resolve()
        run('cmake', '--install', build_dir, '--prefix', stage)
        version = (build_dir / 'generated/version.txt').read_text(encoding='utf-8').strip()
        package(stage, output, target, version)
        if target in INSTALLER_TARGETS:
            # Build the per-user installer from the same verified install tree,
            # then install, check PATH registration and uninstall it.
            installer = build_installer(stage, output, target, version)
            run('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                ROOT / 'tests/integration/windows-installer.ps1',
                '-Installer', installer, '-Version', version)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, choices=TARGETS)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-ci')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--stage', choices=STAGES, default='all',
                        help="'tests' (CTest and package) and 'examples' run concurrently in CI")
    args = parser.parse_args()
    build(args.target, args.build_dir.resolve(), args.output.resolve(), args.stage)
