#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Verify whole-project delegation with real CMake rules, tools and presets."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class CMakeProjectTests(unittest.TestCase):
    """Fixtures are top-level projects; no network or framework download is needed."""

    def setUp(self):
        work = None
        if os.name == 'nt':
            work = Path(__file__).resolve().parents[1] / 'work'
            work.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='cmake project ', dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.write('main.cpp', '''#include "resources.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--failure") return 17;
  if (argc > 1 && std::string(argv[1]) == "--environment")
    return std::getenv("CLUTCH_TEST_PRESET") ? 0 : 19;
  std::cout << "PROVIDER_VALUE=" << RESOURCE_VALUE << "\\n";
  return RESOURCE_VALUE == 42 ? 0 : 1;
}
''')
        self.write('resources.in', '#define RESOURCE_VALUE 42\n')
        self.write('verify.cmake', '''if(NOT "$ENV{CLUTCH_BUILD_PRESET}" STREQUAL "present")
  message(FATAL_ERROR "build preset environment was lost")
endif()
''')
        self.write('CMakeLists.txt', '''cmake_minimum_required(VERSION 3.21)
project(ProviderFixture VERSION 1.2.3 LANGUAGES CXX)
if(NOT PROJECT_IS_TOP_LEVEL OR NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "original top-level context was lost")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.hpp"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/resources.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.hpp"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources.in" VERBATIM)
# A framework-like helper owns target creation and product metadata.
function(fixture_add_app name)
  add_executable(${name} main.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/resources.hpp")
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set_target_properties(${name} PROPERTIES OUTPUT_NAME "product-name")
endfunction()
fixture_add_app(app)
add_custom_target(verify_environment
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/verify.cmake" VERBATIM)
enable_testing()
add_test(NAME "passes[42]" COMMAND app)
if(EXPECT_PRESET)
  add_test(NAME environment COMMAND app --environment)
endif()
install(TARGETS app RUNTIME DESTINATION bin COMPONENT Runtime)
set(CPACK_GENERATOR ZIP)
include(CPack)
''')

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def cli(self, *arguments, success=True, code=None):
        result = subprocess.run([OPTIONS.clutch, *arguments], cwd=self.root,
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        if code is not None:
            self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, *arguments):
        return self.cli('.', '--generator', 'Ninja',
                        '--cmake-arg=-DCMAKE_MAKE_PROGRAM=' + OPTIONS.ninja,
                        '--cmake-arg=-DCMAKE_CXX_COMPILER=' + OPTIONS.cxx, *arguments)

    def locate(self, *arguments):
        return json.loads(self.cli('locate', *arguments))

    def test_top_level_custom_rules_run_test_install_package(self):
        self.configure('--targets')
        location = self.locate()
        binary = Path(location['build'])
        self.assertEqual(binary, self.root / 'Clutch-build-debug')
        self.assertTrue(Path(location['compilationDatabase']).is_file())
        self.assertTrue(Path(location['fileApiIndex']).is_file())
        self.assertIn('verify_environment  UTILITY', self.cli('targets'))
        self.assertIn('PROVIDER_VALUE=42', self.cli('run', '--target', 'app', '--jobs', '2'))
        self.cli('run', '--target', 'app', '--', '--failure', success=False, code=17)
        self.cli('test', '--filter', 'passes[42]', '--jobs', '2')
        self.cli('install', '--prefix', str(self.root / 'stage'), '--component', 'Runtime')
        self.assertTrue(list((self.root / 'stage/bin').glob('product-name*')))
        self.cli('package')
        self.assertTrue(list(binary.glob('ProviderFixture-*.zip')))
        self.cli('clean')
        self.assertIn('PROVIDER_VALUE=42', self.cli('run', '--clean'))

    def test_failure_boundaries_and_regenerated_artifact(self):
        self.configure()
        self.cli('run')
        artifact = next((self.root / 'Clutch-build-debug').glob('product-name*'))
        before = artifact.read_bytes()
        self.cli('run', '--clean', '--target', 'missing', success=False)
        self.assertEqual(artifact.read_bytes(), before)
        original = (self.root / 'CMakeLists.txt').read_text()
        self.write('CMakeLists.txt', original + '\nset_target_properties(app PROPERTIES OUTPUT_NAME renamed)\n')
        self.assertIn('PROVIDER_VALUE=42', self.cli('run'))
        self.assertTrue(list((self.root / 'Clutch-build-debug').glob('renamed*')))
        self.write('main.cpp', '#error intentional compile failure\n')
        failed = self.cli('run', success=False)
        self.assertNotIn('PROVIDER_VALUE=42', failed)
        self.write('CMakeLists.txt', original + '\nmessage(FATAL_ERROR "intentional configure failure")\n')
        self.cli('.', success=False)
        self.assertIn('last CMake configuration', self.cli('build', success=False))
        self.write('CMakeLists.txt', original)
        self.configure()
        self.cli('targets')

    def test_presets_keep_binary_directory_and_independent_workflows(self):
        self.write('CMakePresets.json', json.dumps({
            'version': 3,
            'configurePresets': [{
                'name': 'configured', 'generator': 'Ninja',
                'binaryDir': '${sourceDir}/preset output',
                'cacheVariables': {'CMAKE_MAKE_PROGRAM': OPTIONS.ninja,
                                   'CMAKE_CXX_COMPILER': OPTIONS.cxx,
                                   'CMAKE_BUILD_TYPE': 'RelWithDebInfo',
                                   'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON', 'EXPECT_PRESET': 'ON'},
            }],
            'buildPresets': [{'name': 'compiled', 'configurePreset': 'configured',
                              'targets': ['app', 'verify_environment'],
                              'environment': {'CLUTCH_BUILD_PRESET': 'present'}}],
            'testPresets': [{'name': 'tested', 'configurePreset': 'configured',
                             'environment': {'CLUTCH_TEST_PRESET': 'present'}}],
        }))
        self.cli('.', '--preset', 'configured')
        self.assertEqual(Path(self.locate()['build']), self.root / 'preset output')
        self.assertEqual(self.locate()['configuration'], 'RelWithDebInfo')
        self.cli('build', '--preset', 'compiled')
        self.cli('test', '--preset', 'tested')
        self.cli('build', '--preset', 'configured', success=False)
        self.cli('run', '--target', 'app')
        self.cli('build', '--preset', 'compiled', '--clean', success=False)

    def test_ambiguous_executables_and_unavailable_packaging(self):
        original = (self.root / 'CMakeLists.txt').read_text()
        self.write('CMakeLists.txt', original.replace('include(CPack)', '') +
                   '\nadd_executable(other main.cpp)\n'
                   'target_include_directories(other PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")\n')
        self.configure()
        self.assertIn('one executable', self.cli('run', success=False))
        self.assertFalse(list((self.root / 'Clutch-build-debug').glob('product-name*')))
        self.cli('run', '--target', 'app')
        self.assertIn('does not define CPack', self.cli('package', success=False))

    def test_explicit_package_preset(self):
        version = json.loads(subprocess.check_output(
            [shutil.which('cmake'), '-E', 'capabilities'], text=True))['version']
        if (version['major'], version['minor']) < (3, 25):
            self.skipTest('package presets require CMake 3.25+')
        self.write('CMakePresets.json', json.dumps({
            'version': 6,
            'configurePresets': [{'name': 'configured', 'generator': 'Ninja',
                                  'binaryDir': '${sourceDir}/packaging',
                                  'cacheVariables': {'CMAKE_MAKE_PROGRAM': OPTIONS.ninja,
                                                     'CMAKE_CXX_COMPILER': OPTIONS.cxx}}],
            'packagePresets': [{'name': 'distributed', 'configurePreset': 'configured',
                                'generators': ['ZIP'],
                                'variables': {'CPACK_PACKAGE_FILE_NAME': 'preset-package'}}],
        }))
        self.cli('.', '--preset', 'configured')
        self.cli('build')
        self.cli('package', '--preset', 'distributed')
        self.assertTrue((self.root / 'packaging/preset-package.zip').is_file())

    def test_release_profile_is_separate_and_preserves_debug(self):
        self.configure()
        debug = self.locate()
        self.configure('--release')
        release = self.locate('--release')
        self.assertEqual(Path(release['build']), self.root / 'Clutch-build-release')
        self.assertEqual(release['configuration'], 'Release')
        self.assertNotEqual(debug['build'], release['build'])
        self.assertIn('PROVIDER_VALUE=42', self.cli('run', '--release'))
        self.cli('clean', '--release')
        self.assertEqual(self.locate(), debug)

    def test_metadata_reference_escape_and_redirected_routing(self):
        self.configure()
        location = self.locate()
        index = Path(location['fileApiIndex'])
        content = json.loads(index.read_text())
        content['reply']['client-clutch-project']['codemodel-v2']['jsonFile'] = '../outside.json'
        index.write_text(json.dumps(content), encoding='utf-8')
        self.assertIn('invalid CMake File API reply reference', self.cli('targets', success=False))
        self.configure()
        self.cli('.', '--cmake-arg=-Boutside', success=False)
        self.cli('.', '--offline', success=False)
        self.cli('.', '--provider', 'unknown', success=False)

    def test_multi_configuration_and_explicit_native_selection(self):
        self.cli('.', '--generator', 'Ninja Multi-Config',
                 '--cmake-arg=-DCMAKE_MAKE_PROGRAM=' + OPTIONS.ninja,
                 '--cmake-arg=-DCMAKE_CXX_COMPILER=' + OPTIONS.cxx)
        self.assertEqual(self.locate()['configuration'], 'Debug')
        self.cli('run', '--configuration', 'Release')
        self.cli('run', '--configuration', 'DoesNotExist', success=False)
        self.write('clutch.toml', '[project]\nname="native"\nentry="missing.cpp"\n')
        self.cli('targets', success=False)
        self.assertIn('app  EXECUTABLE', self.cli('targets', '--provider', 'cmake'))

    def test_native_ide_generator(self):
        """Delegate native IDE projects without claiming an exact command database."""
        if sys.platform == 'darwin' and shutil.which('xcodebuild'):
            # Exercise a real bundle artifact while keeping CI signing explicit
            # in the original project, rather than overriding it in the provider.
            original = (self.root / 'CMakeLists.txt').read_text()
            self.write('CMakeLists.txt', original + '\nset_target_properties(app PROPERTIES '
                       'MACOSX_BUNDLE TRUE XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)\n')
            self.cli('.', '--generator', 'Xcode')
        elif os.name == 'nt' and Path(OPTIONS.cxx).stem.lower() == 'cl':
            # CMake selects the installed Visual Studio instance and version.
            self.cli('.')
        else:
            self.skipTest('a native Visual Studio or Xcode installation is required')
        location = self.locate()
        self.assertIsNone(location['compilationDatabase'])
        (Path(location['build']) / 'compile_commands.json').write_text('[]', encoding='utf-8')
        self.assertIsNone(self.locate()['compilationDatabase'])
        self.assertIn('PROVIDER_VALUE=42', self.cli('run', '--target', 'app', '--jobs', '2'))
        self.cli('test', '--filter', 'passes[42]')

    def test_profiles_can_share_a_multi_configuration_preset_tree(self):
        self.write('CMakePresets.json', json.dumps({
            'version': 3,
            'configurePresets': [{'name': 'shared', 'generator': 'Ninja Multi-Config',
                                  'binaryDir': '${sourceDir}/shared tree',
                                  'cacheVariables': {'CMAKE_MAKE_PROGRAM': OPTIONS.ninja,
                                                     'CMAKE_CXX_COMPILER': OPTIONS.cxx}}],
        }))
        self.cli('.', '--preset', 'shared')
        self.cli('.', '--preset', 'shared', '--release')
        self.assertEqual(self.locate()['build'], self.locate('--release')['build'])
        self.assertEqual(self.locate()['configuration'], 'Debug')
        self.assertEqual(self.locate('--release')['configuration'], 'Release')
        self.cli('run')
        self.cli('run', '--release')

    def test_shipped_cmake_project_example(self):
        example = Path(__file__).resolve().parents[2] / 'examples/cmake-project'
        destination = self.root / 'shipped example'
        shutil.copytree(example, destination,
                        ignore=shutil.ignore_patterns('Clutch-build-*', '.clutch'))
        self.root = destination
        self.configure()
        self.assertIn('Hello from the original CMake project!', self.cli('run', '--target', 'hello'))
        self.cli('test')
        self.cli('package')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clutch', required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--ninja', required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.clutch = str(Path(OPTIONS.clutch).resolve())
    unittest.main(argv=[sys.argv[0], *remaining])
