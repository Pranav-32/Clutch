#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise local sources and native package adapters without network services."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

import dependencies_integration as fixtures


class ForeignTests(unittest.TestCase):
    """Real CMake/Make libraries intentionally have no clutch.toml."""

    setUp = fixtures.DependencyTests.setUp
    write = fixtures.DependencyTests.write
    cli = fixtures.DependencyTests.cli
    configure = fixtures.DependencyTests.configure
    manifest = fixtures.DependencyTests.manifest
    git = fixtures.DependencyTests.git
    repo = fixtures.DependencyTests.repo
    tag = fixtures.DependencyTests.tag

    @staticmethod
    def dependency(**settings):
        return '{ ' + ', '.join(f'{key} = {json.dumps(value)}' for key, value in settings.items()) + ' }'

    def test_local_headers_without_git(self):
        headers = self.root / 'plain headers'
        self.write(headers, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        self.write(self.app, 'main.cpp', '#include <value.hpp>\nint main() { return VALUE == 42 ? 0 : 1; }\n')
        self.manifest({'headers': self.dependency(path='../plain headers')})
        self.configure()
        self.cli('run', '--jobs', '2')
        lock = (self.app / 'clutch.lock').read_text()
        self.assertIn('provider = "path"', lock)
        self.assertNotIn(self.root.as_posix(), lock)
        self.configure('ninja', '--locked', '--offline')
        self.assertFalse((self.app / '.clutch/dependency-cache.toml').exists())

    def cmake_library(self):
        library = self.root / 'cmake library'
        self.write(library, 'include/public.hpp', '#pragma once\nint answer();\n')
        self.write(library, 'answer.cpp', '#include "generated.hpp"\nint base();\n'
                   '#ifndef PRIVATE_LIBRARY\n#error missing private library policy\n#endif\n'
                   'int answer() { return base() + GENERATED_VALUE; }\n')
        self.write(library, 'base.cpp', 'int base() { return 40; }\n')
        self.write(library, 'generated.in', '#define GENERATED_VALUE 2\n')
        self.write(library, 'CMakeLists.txt', '''cmake_minimum_required(VERSION 3.21)
project(Answer LANGUAGES CXX)
add_library(base STATIC base.cpp)
add_library(answer STATIC answer.cpp)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.hpp"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/generated.in"
      "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.hpp"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated.in")
target_sources(answer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/generated.hpp")
target_include_directories(answer PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(answer PUBLIC PUBLIC_LIBRARY=42 PRIVATE PRIVATE_LIBRARY=1)
target_compile_definitions(answer INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:ONLY_CPP>"
  "$<$<COMPILE_LANGUAGE:C>:ONLY_C>")
target_compile_features(answer PUBLIC cxx_std_20)
target_compile_options(answer INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/DOPTION_PUBLIC=42>"
  "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-DOPTION_PUBLIC=42>")
target_link_libraries(answer PRIVATE base)
add_library(Answer::Answer ALIAS answer)
''')
        self.write(self.app, 'main.cpp', '#include <public.hpp>\n'
                   '#ifdef PRIVATE_LIBRARY\n#error private definition leaked\n#endif\n'
                   '#if PUBLIC_LIBRARY != 42\n#error missing public interface\n#endif\n'
                   '#if OPTION_PUBLIC != 42\n#error missing public compile options\n#endif\n'
                   '#if !defined(ONLY_CPP) || defined(ONLY_C)\n#error wrong language interface\n#endif\n'
                   '#if defined(_MSVC_LANG)\nstatic_assert(_MSVC_LANG >= 202002L);\n'
                   '#else\nstatic_assert(__cplusplus >= 202002L);\n#endif\n'
                   'int main() { return answer() == 42 ? 0 : 1; }\n')
        return library

    def test_cmake_native_graph_generated_headers_and_incremental_link(self):
        library = self.cmake_library()
        generators = ['ninja']
        if os.name != 'nt' and shutil.which('make'):
            generators.append('make')
        elif Path(OPTIONS.cxx).stem.lower() == 'cl':
            generators.append('visual-studio')
        for generator in generators:
            with self.subTest(generator=generator):
                self.write(library, 'generated.in', '#define GENERATED_VALUE 2\n')
                if (self.app / 'out').exists():
                    shutil.rmtree(self.app / 'out')
                self.manifest({'answer': self.dependency(path='../cmake library', target='Answer::Answer')}, generator)
                self.configure(generator)
                self.assertFalse(list((self.app / 'out/.clutch/foreign').rglob('libanswer.a')))
                self.cli('run', '--jobs', '2')
                artifacts = [p for p in (self.app / 'out/.clutch/foreign').rglob('*')
                             if p.name in ('libanswer.a', 'answer.lib')]
                self.assertTrue(artifacts)
                stamps = {p: p.stat().st_mtime_ns for p in artifacts}
                self.cli('build', '--jobs', '2')
                self.assertEqual(stamps, {p: p.stat().st_mtime_ns for p in artifacts})
                self.write(library, 'generated.in', '#define GENERATED_VALUE (1 + 1)\n')
                self.cli('run', '--jobs', '2')
                self.assertTrue(any(p.stat().st_mtime_ns != before for p, before in stamps.items()))
                commands = json.loads((self.app / 'out/compile_commands.json').read_text())
                command = next(c for c in commands if Path(c['file']).name == 'main.cpp')
                self.assertTrue(any('PUBLIC_LIBRARY=42' in arg for arg in command['arguments']))
                self.assertFalse(any('PRIVATE_LIBRARY' in arg for arg in command['arguments']))
                self.configure(generator, '--locked', '--offline')

    def installed_package(self, root, package='Widget'):
        self.write(root, 'include/widget.hpp', '#pragma once\n#define SDK_VALUE 42\n')
        self.write(root, f'lib/cmake/{package}/{package}Config.cmake',
                   f'set({package}_VERSION 1.2.3)\nadd_library({package}::Widget INTERFACE IMPORTED)\n'
                   f'set_target_properties({package}::Widget PROPERTIES\n'
                   ' INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include"\n'
                   ' INTERFACE_COMPILE_DEFINITIONS SDK_PUBLIC=42)\n')

    def test_installed_sdk_interface_and_lock(self):
        sdk = self.root / 'installed sdk'
        self.installed_package(sdk)
        self.write(self.app, 'main.cpp', '#include <widget.hpp>\n'
                   'int main() { return SDK_VALUE == SDK_PUBLIC ? 0 : 1; }\n')
        self.manifest({'widget': self.dependency(system='Widget', root=sdk.as_posix(), target='Widget::Widget')})
        self.configure()
        self.cli('run', '--jobs', '2')
        lock = (self.app / 'clutch.lock').read_text()
        self.assertIn('provider = "system"', lock)
        self.assertIn('version = "1.2.3"', lock)
        self.assertNotIn(sdk.as_posix(), lock)
        self.configure('ninja', '--locked', '--offline')

    def test_explicit_system_headers_without_cmake_metadata(self):
        """An arbitrary logical SDK identity has no package-specific discovery code."""
        sdk = self.root / 'vendor prefix'
        self.write(sdk, 'headers/api.hpp', '#pragma once\n#define SDK_VALUE 42\n')
        self.write(self.app, 'main.cpp', '#include <api.hpp>\n'
                   'int main() { return SDK_VALUE == SDK_PUBLIC ? 0 : 1; }\n')
        self.manifest({'api': self.dependency(system='Any Vendor SDK', root=sdk.as_posix(),
                                               include_dirs=['headers'], defines=['SDK_PUBLIC=42'])})
        self.configure()
        self.cli('run')
        self.assertFalse((self.app / 'out/.clutch/foreign').exists())
        self.configure('ninja', '--locked', '--offline')

    def test_explicit_system_library_files(self):
        """Preinstalled native archives are link inputs, without a package config."""
        sdk = self.root / 'binary sdk'
        self.write(sdk, 'headers/api.hpp', 'extern "C" int vendor_answer(void);\n')
        self.write(sdk, 'source/answer.c', 'int vendor_answer(void) { return 42; }\n')
        self.write(sdk, 'source/CMakeLists.txt', 'cmake_minimum_required(VERSION 3.21)\n'
                   'project(InstalledFixture LANGUAGES C)\nadd_library(vendor STATIC answer.c)\n'
                   'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/artifact.txt" CONTENT "$<TARGET_FILE:vendor>")\n')
        command = ['cmake', '-S', str(sdk / 'source'), '-B', str(sdk / 'lib'), '-G', 'Ninja',
                   f'-DCMAKE_MAKE_PROGRAM={OPTIONS.ninja}', f'-DCMAKE_C_COMPILER={OPTIONS.cc}',
                   '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug']
        for args in (command, ['cmake', '--build', str(sdk / 'lib')]):
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        artifact = Path((sdk / 'lib/artifact.txt').read_text(encoding='utf-8'))
        self.write(self.app, 'main.cpp', '#include <api.hpp>\n'
                   'int main() { return vendor_answer() == 42 ? 0 : 1; }\n')
        # Two aliases share one logical SDK while selecting distinct interfaces.
        self.manifest({'headers': self.dependency(system='VendorBinarySdk', root=sdk.as_posix(), include_dirs=['headers']),
                       'binary': self.dependency(system='VendorBinarySdk', root=sdk.as_posix(),
                                                 libraries=[artifact.relative_to(sdk).as_posix()])})
        self.configure()
        self.cli('run')
        self.assertFalse((self.app / 'out/.clutch/foreign').exists())
        self.configure('ninja', '--locked', '--offline')

    def test_system_interface_paths_in_native_dependency(self):
        """SDK paths stay relative to the manifest declaring a transitive import."""
        library = self.root / 'native library'
        self.write(library, 'sdk/api.hpp', '#pragma once\n#define SDK_VALUE 42\n')
        self.write(library, 'sdk/prefix.hpp', '#pragma once\n#define SDK_PREFIX 42\n')
        self.write(library, 'sdk/runtime.dat', 'runtime fixture')
        self.write(library, 'answer.cpp', '#include <api.hpp>\n'
                   'int answer() { return SDK_VALUE == SDK_PREFIX ? 42 : 0; }\n')
        self.write(library, 'clutch.toml', '[project]\nname="answer"\nversion="1.0.0"\n'
                   'language="cpp"\nstd=17\n[[target]]\nname="answer"\ntype="static"\n'
                   'sources=["answer.cpp"]\n[dependencies]\n'
                   'sdk={system="Nested SDK",include_dirs=["sdk"],pch=["sdk/prefix.hpp"],'
                   'runtime=["sdk/runtime.dat"]}\n')
        self.write(self.app, 'main.cpp', 'int answer();\nint main() { return answer() == 42 ? 0 : 1; }\n')
        self.manifest({'answer': self.dependency(path='../native library', target='answer')})
        self.configure()
        self.cli('run')
        if os.name == 'nt':
            self.assertEqual((self.app / 'out/runtime.dat').read_text(encoding='utf-8'), 'runtime fixture')
        self.configure('ninja', '--locked', '--offline')

    def test_explicit_system_contract_validation(self):
        """Do not silently mix imported targets, literal libraries and version probes."""
        for settings, diagnostic in (
                ({'system': 'Vendor', 'libraries': ['example'], 'target': 'Vendor::Core'}, 'not both'),
                ({'system': 'Vendor', 'libraries': ['-bad-switch']}, 'invalid system library'),
                ({'system': 'Vendor', 'libraries': ['example'], 'version': '1.0'}, 'cannot use CMake'),
                ({'system': 'Vendor'}, 'needs target/targets')):
            with self.subTest(settings=settings):
                self.manifest({'api': self.dependency(**settings)})
                self.assertIn(diagnostic, self.configure(success=False))

    def test_cmake_shared_library_runtime(self):
        library = self.root / 'shared library'
        self.write(library, 'value.cpp', 'int answer() { return 42; }\n')
        self.write(library, 'CMakeLists.txt', 'cmake_minimum_required(VERSION 3.21)\n'
                   'project(Answer LANGUAGES CXX)\nadd_library(answer SHARED value.cpp)\n'
                   'set_target_properties(answer PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)\n')
        self.write(self.app, 'main.cpp', 'int answer();\nint main() { return answer() == 42 ? 0 : 1; }\n')
        self.manifest({'answer': self.dependency(path='../shared library', target='answer')})
        self.configure()
        self.cli('run', '--jobs', '2')
        if os.name == 'nt':
            self.assertTrue((self.app / 'out/answer.dll').is_file())

    def test_c_consumer_uses_cpp_linker_for_foreign_library(self):
        library = self.root / 'cpp library'
        self.write(library, 'answer.cpp', '#include <string>\n'
                   'extern "C" int answer() { return static_cast<int>(std::string("answer").size()); }\n')
        self.write(library, 'CMakeLists.txt', 'cmake_minimum_required(VERSION 3.21)\n'
                   'project(Answer LANGUAGES CXX)\nadd_library(answer STATIC answer.cpp)\n')
        self.write(self.app, 'main.c', 'int answer(void);\nint main(void) { return answer() == 6 ? 0 : 1; }\n')
        self.write(self.app, 'clutch.toml', '[project]\nname="app"\nversion="1.0.0"\n'
                   'language="c"\nstd=11\nentry="main.c"\n[build]\nsources=["main.c"]\n'
                   '[profile.debug]\nbuild_dir="out"\n[dependencies]\nanswer=' +
                   self.dependency(path='../cpp library', target='answer') + '\n')
        self.configure()
        self.cli('run', '--jobs', '2')

    def test_header_only_transitive_cmake_interface(self):
        self.cmake_library()
        wrapper = self.root / 'header wrapper'
        self.write(wrapper, 'include/wrapper.hpp', '#pragma once\n')
        self.write(wrapper, 'clutch.toml', '[dependencies]\nanswer=' +
                   self.dependency(path='../cmake library', target='Answer::Answer') + '\n')
        self.manifest({'wrapper': self.dependency(path='../header wrapper', build_system='header-only')})
        self.configure()
        self.cli('run', '--jobs', '2')

    def test_language_specific_public_interface(self):
        self.cmake_library()
        self.write(self.app, 'helper.c', '#if !defined(ONLY_C) || defined(ONLY_CPP)\n'
                   '#error wrong C interface\n#endif\nint helper(void) { return 0; }\n')
        self.manifest({'answer': self.dependency(path='../cmake library', target='Answer::Answer')},
                      extra='sources=["main.cpp", "helper.c"]')
        manifest = self.app / 'clutch.toml'
        manifest.write_text(manifest.read_text().replace('language="cpp"', 'language="mixed"')
                            .replace('std=17', 'std={c=11, cpp=17}'), encoding='utf-8')
        query = self.app / 'out/.clutch/api/v1/query'
        query.mkdir(parents=True)
        (query / 'codemodel-v1').touch()
        self.configure()
        replies = self.app / 'out/.clutch/api/v1/reply'
        targets = list(replies.glob('generation-*/target-*.json'))
        self.assertTrue(targets)
        target = json.loads(targets[0].read_text())
        groups = {group['language']: group['defines'] for group in target['compileGroups']}
        self.assertIn('ONLY_C', groups['C'])
        self.assertNotIn('ONLY_CPP', groups['C'])
        self.assertIn('ONLY_CPP', groups['CXX'])
        self.assertNotIn('ONLY_C', groups['CXX'])
        action = target['actions']['build']
        result = subprocess.run(action['arguments'], cwd=action['directory'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.cli('run', '--jobs', '2')

    @unittest.skipUnless(os.name == 'nt', 'DirectX requires the Windows SDK')
    def test_installed_directx_sdk(self):
        if Path(OPTIONS.cxx).stem.lower() not in ('cl', 'clang-cl'):
            self.skipTest('Windows SDK fixture requires an MSVC compiler environment')
        self.write(self.app, 'main.cpp', '#include <d3d12.h>\n'
                   'int main() { auto *volatile factory = &D3D12CreateDevice; return factory ? 0 : 1; }\n')
        self.manifest({'graphics': self.dependency(system='windows-sdk', libraries=['d3d12']),
                       'factory': self.dependency(system='windows-sdk', libraries=['dxgi'])})
        self.configure()
        self.cli('run', '--jobs', '2')

    @unittest.skipIf(os.name == 'nt', 'fixture uses GNU driver flags')
    def test_explicit_makefile_interface(self):
        library = self.root / 'make library'
        self.write(library, 'include/answer.hpp', '#pragma once\nint answer();\n')
        self.write(library, 'answer.cpp', 'int answer() { return 42; }\n')
        self.write(library, 'Makefile', 'empty :=\nspace := $(empty) $(empty)\n'
                   'source := $(subst $(space),\\$(space),$(SOURCE_DIR))\n'
                   'all: libanswer.a\n'
                   'answer.o: $(source)/answer.cpp\n\t"$(CXX)" -c "$(SOURCE_DIR)/answer.cpp" -o "$@"\n'
                   'libanswer.a: answer.o\n\tar rcs "$@" "$<"\n')
        self.write(self.app, 'main.cpp', '#include <answer.hpp>\nint main() { return answer() == 42 ? 0 : 1; }\n')
        self.manifest({'answer': self.dependency(path='../make library', build_system='make',
                                               libraries=['libanswer.a'], include_dirs=['include'])})
        self.configure()
        self.cli('run', '--jobs', '2')
        self.assertFalse((library / 'answer.o').exists())
        self.cli('run', '--jobs', '2')

    def test_makefile_requires_public_artifact_contract(self):
        library = self.root / 'make library'
        self.write(library, 'Makefile', 'all:\n\t@echo do not execute during configure\n')
        self.manifest({'bad': self.dependency(path='../make library', build_system='make')})
        self.assertIn('requires libraries', self.configure(success=False))

    def test_invalid_source_combinations(self):
        self.manifest({'bad': self.dependency(path='.', system='Widget')})
        self.assertIn('exactly one', self.configure(success=False))

    def test_named_catalog_import_without_vcpkg_executable(self):
        library = self.repo('upstream')
        self.write(library, 'include/value.hpp', '#pragma once\n#define VALUE 42\n')
        self.tag(library, 'v1.0.0')
        registry = self.repo('registry')
        self.write(registry, 'ports/answer/vcpkg.json', json.dumps({'name': 'answer', 'version': '1.0.0'}))
        self.write(registry, 'ports/answer/portfile.cmake',
                   'vcpkg_from_git(\n OUT_SOURCE_PATH SOURCE_PATH\n'
                   f' URL "{library.as_posix()}"\n REF "v${{VERSION}}"\n)\n')
        baseline = self.tag(registry, 'catalog-v1')
        self.write(self.app, 'main.cpp', '#include <value.hpp>\nint main() { return VALUE == 42 ? 0 : 1; }\n')
        self.manifest({'answer': self.dependency(vcpkg='answer', root=registry.as_posix(), baseline=baseline)})
        self.configure()
        self.cli('run', '--jobs', '2')
        lock = (self.app / 'clutch.lock').read_text()
        self.assertIn(baseline, lock)
        self.assertIn(library.as_posix(), lock)
        self.configure('ninja', '--locked', '--offline')
        self.assertFalse((self.app / 'out/.clutch/vcpkg/installed').exists())

    def test_named_catalog_patches_are_not_silently_discarded(self):
        registry = self.repo('registry')
        self.write(registry, 'ports/answer/vcpkg.json', json.dumps({'name': 'answer', 'version': '1.0.0'}))
        self.write(registry, 'ports/answer/portfile.cmake',
                   'vcpkg_from_github(OUT_SOURCE_PATH SOURCE_PATH REPO example/answer REF "v${VERSION}" '
                   'SHA512 unused PATCHES fix.patch)\n')
        baseline = self.tag(registry, 'catalog-v1')
        self.manifest({'answer': self.dependency(vcpkg='answer', root=registry.as_posix(), baseline=baseline)})
        output = self.configure(success=False)
        self.assertIn('requires patches', output)
        self.assertIn('upstream Git repository', output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('clutch', 'cc', 'cxx', 'ninja'):
        parser.add_argument('--' + field, required=True)
    OPTIONS, remaining = parser.parse_known_args()
    fixtures.OPTIONS = OPTIONS
    unittest.main(argv=[sys.argv[0], *remaining])
