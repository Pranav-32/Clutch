#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise independent symbols, real safety runtimes and developer-tool argv."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TIMEOUT = 120


class ToolingSafetyTests(unittest.TestCase):
    """Use actual compilers and isolated manifests; optional tools stay optional."""

    def setUp(self):
        parent = ROOT / 'tests/work' if os.name == 'nt' else None
        if parent:
            parent.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='tooling safety ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.write('main.cpp', 'int main() { return 0; }\n')
        self.manifest()

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
        return path

    def manifest(self, debug='', release='', extra=''):
        self.write('clutch.toml', '[project]\nname="safe_app"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\n[build]\nsources=["main.cpp"]\n'
                   '[profile.debug]\nbuild_dir="out-debug"\n' + debug +
                   '\n[profile.release]\nbuild_dir="out-release"\n' + release + '\n' + extra)

    def cli(self, *arguments, success=True):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                capture_output=True, text=True, timeout=TIMEOUT)
        if success is not None:
            self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def configure(self, release=False, success=True):
        return self.cli('.', '--generator', 'ninja', '--build-tool', OPTIONS.ninja,
                        '--cc', OPTIONS.cc, '--cxx', OPTIONS.cxx,
                        *(['--release'] if release else []), success=success)

    def arguments(self, release=False):
        build = self.root / ('out-release' if release else 'out-debug')
        return json.loads((build / 'compile_commands.json').read_text())[0]['arguments']

    def require_safety(self, release=False):
        """Missing optional runtimes must be diagnosed; record an explicit skip."""
        result = self.configure(release=release, success=None)
        if result.returncode:
            self.assertIn('requested profile safety is unavailable', result.stderr)
            self.skipTest('selected safety runtime unavailable: ' + result.stderr.splitlines()[0])

    def test_symbols_are_independent_of_profile_and_optimization(self):
        """All four combinations retain optimization/runtime profile selection."""
        microsoft = Path(OPTIONS.cxx).stem.lower() in ('cl', 'clang-cl')
        for release in (False, True):
            for symbols in (False, True):
                with self.subTest(release=release, symbols=symbols):
                    policy = f'debug_symbols={str(symbols).lower()}\n'
                    self.manifest(debug=policy, release=policy)
                    self.configure(release)
                    arguments = self.arguments(release)
                    self.assertEqual('/Z7' in arguments if microsoft else '-g' in arguments,
                                     symbols)
                    self.assertIn(('/O2' if release else '/Od') if microsoft
                                  else ('-O3' if release else '-O0'), arguments)
                    self.cli('build', *(['--release'] if release else []))
                    if symbols and (microsoft or (os.name == 'nt' and any(
                            flag.startswith('-fms-runtime-lib=') for flag in arguments))):
                        build = self.root / ('out-release' if release else 'out-debug')
                        self.assertTrue(list(build.glob('*.pdb')))

    def test_policy_validation_includes_inactive_profiles(self):
        """Misspellings and incompatible detectors cannot silently disable safety."""
        for policy in ('debug_symbols="true"', 'safety=true',
                       'safety={sanitizers=["memory"]}',
                       'safety={sanitizers=["address","address"]}',
                       'safety={sanitizers=["address","thread"]}',
                       'safety={strictness="maximum"}', 'safety={lifetime="true"}',
                       'safety={borrow_checker=true}'):
            with self.subTest(policy=policy):
                self.manifest(release=policy)
                self.configure(success=False)

    def test_runtime_undefined_behavior_is_detected(self):
        """Strict UBSan must link its runtime and fail on real signed overflow."""
        if os.name == 'nt':
            self.skipTest('UBSan runtime execution is checked on POSIX hosts')
        self.manifest(debug='safety={sanitizers=["undefined"],strictness="strict"}\n')
        self.write('main.cpp', '#include <limits.h>\nint main() { volatile int n = INT_MAX; '
                   'volatile int result = n + 1; return result == 0; }\n')
        self.require_safety()
        self.assertIn('-fno-sanitize-recover=all', self.arguments())
        result = self.cli('run', success=False)
        self.assertIn('signed integer overflow', result.stderr)

    def test_warning_arrays_enable_disable_and_promote_diagnostics(self):
        """A portable category must affect the compiler rather than only metadata."""
        self.write('main.cpp', 'int value=1;\nint main() { const int value=2; return value-2; }\n')
        self.manifest(debug='warnings=["shadow","error"]\n')
        self.configure()
        result = self.cli('build', success=False)
        self.assertTrue('shadow' in result.stderr + result.stdout or
                        'C4459' in result.stderr + result.stdout)
        self.manifest(debug='warnings=["shadow","no-shadow","error"]\n')
        self.configure()
        self.cli('build')
        self.manifest(debug='warnings=[]\n')
        self.configure()
        arguments = self.arguments()
        self.assertNotIn('-Wall', arguments)
        self.assertNotIn('/W4', arguments)
        for setting in ('["unknown"]', '["extra","default"]', '["shadow","shadow"]',
                        '[1]', '["none","shadow"]'):
            with self.subTest(setting=setting):
                self.manifest(release='warnings=' + setting + '\n')
                self.configure(success=False)

    def test_warning_array_survives_workspace_and_saved_configuration(self):
        """Root policy must reach member commands and remain available to IDEs."""
        self.write('member/main.cpp', 'int main() { return 0; }\n')
        self.write('member/clutch.toml', '[project]\nname="member"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\n')
        self.write('clutch.toml', '[workspace]\nmembers=["member"]\n'
                   '[profile.debug]\nbuild_dir="out-debug"\n'
                   'warnings=["extra","conversion","no-unused"]\n')
        self.configure()
        arguments = self.arguments()
        if Path(OPTIONS.cxx).stem.lower() == 'cl':
            self.assertIn('/W4', arguments)
            self.assertIn('/w14244', arguments)
            self.assertIn('/wd4100', arguments)
        elif Path(OPTIONS.cxx).stem.lower() == 'clang-cl':
            self.assertIn('/W4', arguments)
            self.assertIn('/clang:-Wconversion', arguments)
            self.assertIn('/clang:-Wno-unused', arguments)
        else:
            self.assertIn('-Wextra', arguments)
            self.assertIn('-Wconversion', arguments)
            self.assertIn('-Wno-unused', arguments)
        self.cli('build')

    def test_address_runtime_detects_heap_use_after_free(self):
        """A sanitizer flag alone is insufficient: exercise the resulting program."""
        policy = 'safety={sanitizers=["address"]}\n'
        # clang-cl rejects ASan with the debug CRT; do not change CRT policy
        # implicitly. Exercise its supported release configuration instead.
        release = Path(OPTIONS.cxx).stem.lower() == 'clang-cl'
        self.manifest(debug=policy, release=policy)
        self.write('main.cpp', '#include <stdlib.h>\nint main() { int *p = (int*)malloc(sizeof(int)); '
                   'if (!p) return 2; *p = 42; free(p); return *(volatile int*)p; }\n')
        self.require_safety(release=release)
        result = self.cli('run', *(['--release'] if release else []), success=False)
        self.assertIn('heap-use-after-free', result.stderr + result.stdout)

    def test_lifetime_support_is_probed_not_assumed(self):
        """Older Clang and other compilers must fail an explicit lifetime request."""
        self.manifest(debug='warnings="none"\nsafety={lifetime=true,strictness="strict"}\n')
        result = self.configure(success=None)
        if result.returncode:
            self.assertIn('requested profile safety is unavailable', result.stderr)
        else:
            self.assertTrue(any('Werror=lifetime-safety' in flag for flag in self.arguments()))
            self.write('main.cpp', 'int *escape() { int value=1; return &value; }\n'
                       'int main() { return *escape(); }\n')
            result = self.cli('build', success=False)
            self.assertIn('lifetime', result.stderr + result.stdout)

    def test_leak_runtime_reports_lost_allocation(self):
        """Leak detection requires program execution rather than a clean compile."""
        if os.name == 'nt':
            self.skipTest('LeakSanitizer has no Windows runtime')
        self.manifest(debug='safety={sanitizers=["leak"]}\n')
        self.write('main.cpp', '#include <stdlib.h>\nvoid *allocation;\n'
                   'int main() { allocation = malloc(17); if (!allocation) return 2; '
                   'allocation = 0; return 0; }\n')
        self.require_safety()
        result = self.cli('run', success=False)
        self.assertIn('LeakSanitizer', result.stderr)

    def test_cmake_source_dependency_inherits_instrumentation(self):
        """An overflow in upstream source code must use its native build and UBSan."""
        if os.name == 'nt' or not shutil.which('cmake'):
            self.skipTest('POSIX CMake/UBSan instrumentation check')
        self.manifest(debug='safety={sanitizers=["undefined"],strictness="strict"}\n',
                      extra='[dependencies]\nhelper={path="native",target="helper"}\n')
        self.write('main.cpp', 'int helper(); int main() { return helper(); }\n')
        self.write('native/CMakeLists.txt', 'cmake_minimum_required(VERSION 3.21)\n'
                   'project(helper LANGUAGES CXX)\nadd_library(helper STATIC helper.cpp)\n')
        self.write('native/helper.cpp', '#include <limits.h>\nint helper() { '
                   'volatile int n=INT_MAX; return n+1; }\n')
        self.require_safety()
        result = self.cli('run', success=False)
        self.assertIn('signed integer overflow', result.stderr)

    def test_file_api_exposes_symbol_policy_and_debug_action(self):
        """IDE clients receive the same policy as the generated commands."""
        location = json.loads(self.cli('locate').stdout)
        query = Path(location['paths']['query'])
        query.mkdir(parents=True, exist_ok=True)
        (query / 'codemodel-v1').touch()
        for symbols in (False, True):
            self.manifest(debug=f'debug_symbols={str(symbols).lower()}\n')
            self.configure()
            reply = Path(location['paths']['reply'])
            index = json.loads(sorted(reply.glob('index-*.json'))[-1].read_text())
            reference = next(item for item in index['objects'] if item['kind'] == 'codemodel')
            model = json.loads((reply / reference['jsonFile']).read_text())
            self.assertEqual(model['compilePolicy']['debugSymbols'], symbols)
            self.assertEqual(model['compilePolicy']['safety']['sanitizers'], [])
            target = json.loads((reply / model['targets'][0]['jsonFile']).read_text())
            self.assertEqual('debug' in target['actions'], symbols)

    def test_formatter_checks_and_only_changes_selected_project_files(self):
        """Formatting works before configure and respects generated/dependency trees."""
        formatter = shutil.which('clang-format')
        if not formatter:
            self.skipTest('clang-format is an optional developer tool')
        self.write('.clang-format', 'BasedOnStyle: LLVM\n')
        # A child ignore file replaces the repository's tests/work exclusion.
        self.write('.clang-format-ignore', '')
        self.write('main.cpp', 'int  main( ){return 0;}\n')
        self.write('header.hpp', '#pragma once\nint  helper( );\n')
        foreign = self.write('.clutch/deps/foreign.cpp', 'int  foreign( ){return 0;}\n')
        original = foreign.read_bytes()
        self.cli('fmt', '--check', '--tool', formatter, success=False)
        self.cli('fmt', '--tool', formatter)
        self.cli('fmt', '--check', '--tool', formatter)
        self.assertEqual(foreign.read_bytes(), original)
        self.assertEqual((self.root / 'header.hpp').read_text(), '#pragma once\nint helper();\n')
        self.assertFalse((self.root / 'out-debug').exists())

    def tool(self, name, code=0):
        """Compile a native argv recorder; never fake an interactive debugger UI."""
        source = self.write('tool.c', '#include <stdio.h>\nint main(int argc, char **argv) { '
                            'for (int i=1;i<argc;++i) puts(argv[i]); return ' + str(code) + '; }\n')
        output = self.root / (name + ('.exe' if os.name == 'nt' else ''))
        if Path(OPTIONS.cc).stem.lower() in ('cl', 'clang-cl'):
            command = [OPTIONS.cc, '/nologo', str(source), '/Fe' + str(output),
                       '/Fo' + str(self.root / 'tool.obj')]
        else:
            command = [OPTIONS.cc, str(source), '-o', str(output)]
        result = subprocess.run(command, cwd=self.root, capture_output=True,
                                text=True, timeout=TIMEOUT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return str(output)

    def test_lint_uses_database_and_preserves_tool_exit_status(self):
        """The adapter forwards argv and strict diagnostic policy without a shell."""
        self.manifest(debug='safety={strictness="strict"}\n')
        self.configure()
        analyzer = self.tool('clang-tidy', code=7)
        result = self.cli('lint', '--tool', analyzer, '--checks=-*,bugprone-*', success=False)
        self.assertEqual(result.returncode, 7)
        self.assertIn(str(self.root / 'out-debug'), result.stdout)
        self.assertIn('--checks=-*,bugprone-*', result.stdout)
        self.assertIn('--warnings-as-errors=*', result.stdout)
        if real := shutil.which('clang-tidy'):
            self.cli('lint', '--tool', real, '--checks=-*,clang-analyzer-*')

    def test_debug_builds_and_passes_program_arguments(self):
        """Debugging shares run's selection/build lifecycle and preserves argv."""
        self.configure()
        debugger = self.tool('gdb', code=7)
        result = self.cli('debug', '--tool', debugger, '--', 'two words', '--program-flag', success=False)
        self.assertEqual(result.returncode, 7)
        self.assertIn('--args\n', result.stdout)
        self.assertIn('two words\n--program-flag', result.stdout)
        self.assertTrue((self.root / 'out-debug' / ('safe_app.exe' if os.name == 'nt'
                                                 else 'safe_app')).is_file())
        self.manifest(debug='debug_symbols=false\n')
        self.configure()
        result = self.cli('debug', '--tool', debugger, success=False)
        self.assertIn('debug_symbols = true', result.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + option, required=True)
    OPTIONS, arguments = parser.parse_known_args()
    for option in ('clutch', 'cc', 'cxx', 'ninja'):
        value = getattr(OPTIONS, option)
        setattr(OPTIONS, option, os.path.abspath(shutil.which(value) or value))
    unittest.main(argv=[sys.argv[0], *arguments])
