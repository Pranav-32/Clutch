#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Verify two native Clutch generations from one CMake-built bootstrap executable.

The copied source tree has no Git metadata or CMake helper scripts. Outputs are
retained under tests/work (or --work-root) for inspection and further testing.
Python orchestrates this developer check only; neither native generation needs it.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

SOURCE_ROOT = Path(__file__).resolve().parents[2]
COMMAND_TIMEOUT = 900


def program(value):
    """Resolve tools before child working directories change."""
    # clang++ and g++ select C++ linking through argv[0]; resolving the symlink
    # can turn them into clang/gcc and silently drop the C++ runtime libraries.
    return os.path.abspath(shutil.which(value) or value)


def invoke(arguments, directory, env):
    """Preserve diagnostics and the original child status in the retained fixture."""
    result = subprocess.run(arguments, cwd=directory, env=env, text=True,
                            capture_output=True, timeout=COMMAND_TIMEOUT)
    print(result.stdout, end='', flush=True)
    if result.returncode:
        print(result.stderr, end='', flush=True)
        raise RuntimeError(f'command failed ({result.returncode}): {arguments}')
    return result.stdout


def verify(options):
    """CMake bootstrap -> native release -> native debug -> real project build."""
    parent = Path(options.work_root).resolve() if options.work_root else SOURCE_ROOT / 'tests/work'
    parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='self-hosting-', dir=parent))
    source = work / 'source'
    source.mkdir()
    for folder in ('src', 'third_party'):
        shutil.copytree(SOURCE_ROOT / folder, source / folder)
    for name in ('VERSION', 'clutch.toml', 'CMakeLists.txt', 'LICENSE'):
        shutil.copy2(SOURCE_ROOT / name, source / name)

    # The native path must not call CMake. A failing sentinel is placed before
    # ordinary tools; the original CMake project remains beside clutch.toml.
    tools = work / 'tools'
    tools.mkdir()
    marker = work / 'unexpected-cmake-call'
    if os.name == 'nt':
        # Native executable lookup intentionally ignores .cmd scripts. Compile
        # an actual blocker so a later real cmake.exe cannot bypass the guard.
        blocker = tools / 'blocker.c'
        blocker.write_text('#include <stdio.h>\nint main(void) {\n'
                           'FILE *f = fopen("unexpected-cmake-call", "wb");\n'
                           'if (f) { fputs("unexpected", f); fclose(f); }\n'
                           'return 99; }\n', encoding='utf-8')
        sentinel = tools / 'cmake.exe'
        if Path(options.cc).stem.lower() in ('cl', 'clang-cl'):
            command = [options.cc, '/nologo', '/MT', str(blocker),
                       '/Fe' + str(sentinel), '/Fo' + str(tools / 'blocker.obj')]
        else:
            command = [options.cc, str(blocker), '-o', str(sentinel)]
        invoke(command, tools, os.environ.copy())
    else:
        sentinel = tools / 'cmake'
        sentinel.write_text('#!/bin/sh\nprintf unexpected > ' +
                            "'" + str(marker).replace("'", "'\\''") + "'\nexit 99\n", encoding='utf-8')
        sentinel.chmod(0o755)
    env = os.environ.copy()
    env['PATH'] = str(tools) + os.pathsep + env.get('PATH', '')
    current = options.clutch
    expected = 'clutch ' + (source / 'VERSION').read_text(encoding='utf-8').strip() + '+source'
    generations = []
    for profile in ('release', 'debug'):
        flags = ['--release'] if profile == 'release' else []
        invoke([current, '.', '--generator', 'ninja', '--build-tool', options.ninja,
                '--cc', options.cc, '--cxx', options.cxx, *flags], source, env)
        location = json.loads(invoke([current, 'locate', *flags], source, env))
        build = Path(location['paths']['build'])
        invoke([current, 'build', '--jobs', str(options.jobs), *flags], source, env)
        produced = build / ('clutch.exe' if os.name == 'nt' else 'clutch')
        if not produced.is_file():
            raise RuntimeError('native generator did not produce Clutch')
        if invoke([str(produced), '--version'], source, env).strip() != expected:
            raise RuntimeError('self-hosted source-archive identity differs from VERSION')
        database = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
        actual = {Path(unit['file']) for unit in database}
        declared = set((source / 'src').rglob('*.cpp'))
        if actual != declared:
            raise RuntimeError('self-hosting omitted or duplicated native sources')
        if list(build.glob('CMakeCache.txt')) or not (build / 'build.ninja').is_file():
            raise RuntimeError('self-hosting used a delegated configuration')
        # A second build must preserve every unchanged object and generated file.
        objects = {Path(unit['output']): Path(unit['output']).stat().st_mtime_ns for unit in database}
        invoke([current, 'build', '--jobs', str(options.jobs), *flags], source, env)
        if objects != {path: path.stat().st_mtime_ns for path in objects}:
            raise RuntimeError('unchanged self-hosting rebuilt compilation units')
        generations.append(str(produced))
        current = str(produced)

    smoke = work / 'smoke'
    invoke([current, 'new', str(smoke)], work, env)
    invoke([current, '.', '--cc', options.cc, '--cxx', options.cxx,
            '--build-tool', options.ninja], smoke, env)
    output = invoke([current, 'run', '--jobs', str(options.jobs)], smoke, env)
    if 'Hello from Clutch!' not in output or marker.exists() or list(work.rglob(marker.name)):
        raise RuntimeError('self-hosted smoke failed or unexpectedly invoked CMake')
    result = {'source': str(source), 'generations': generations, 'version': expected,
              'cmakeInvoked': False, 'smoke': str(smoke)}
    (work / 'verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: two native generations; fixtures and verification.json: {work}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    parser.add_argument('--work-root', help='Parent for a newly owned retained fixture')
    parser.add_argument('--jobs', type=int, default=2, choices=range(1, 129), metavar='1..128')
    options = parser.parse_args()
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        setattr(options, option, program(getattr(options, option)))
    verify(options)
