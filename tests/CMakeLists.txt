# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
#
# Register native tests by purpose. CTest names remain stable across platforms;
# compiler-backed suites require their actual host tools and a built Clutch.
option(CLUTCH_TEST_SELF_HOSTING "Build two additional native Clutch generations" OFF)

add_test(NAME clutch.help COMMAND clutch --help)
add_test(NAME clutch.version COMMAND clutch --version)
add_executable(clutch_version_tests unit/version_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/core/dependencies/version.cpp")
clutch_target_defaults(clutch_version_tests)
add_test(NAME clutch.version-ranges COMMAND clutch_version_tests)
add_executable(clutch_files_tests unit/files_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/platform/artifacts.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/files.cpp")
clutch_target_defaults(clutch_files_tests)
add_test(NAME clutch.files COMMAND clutch_files_tests "${CMAKE_CURRENT_BINARY_DIR}/file-tests")
add_executable(clutch_git_submodules_tests unit/git_submodules_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/core/dependencies/git_submodules.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/files.cpp")
clutch_target_defaults(clutch_git_submodules_tests)
add_test(NAME clutch.git-submodules COMMAND clutch_git_submodules_tests
        "${CMAKE_CURRENT_BINARY_DIR}/submodule-tests")
set_tests_properties(clutch.git-submodules PROPERTIES LABELS "unit" TIMEOUT 30)
add_executable(clutch_zip_tests unit/zip_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/platform/zip.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/files.cpp")
clutch_target_defaults(clutch_zip_tests)
add_test(NAME clutch.zip COMMAND clutch_zip_tests "${CMAKE_CURRENT_BINARY_DIR}/zip-tests")
set_tests_properties(clutch.zip PROPERTIES LABELS "unit" TIMEOUT 30)

# Unit suites are tool-independent; bounded stress variants are separately selectable.
add_executable(clutch_source_files_tests unit/source_files_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/core/source_files.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/files.cpp")
clutch_target_defaults(clutch_source_files_tests)
add_test(NAME clutch.source-files COMMAND clutch_source_files_tests "${CMAKE_CURRENT_BINARY_DIR}/source-tests")
add_test(NAME clutch.stress-source-files COMMAND clutch_source_files_tests "${CMAKE_CURRENT_BINARY_DIR}/source-tests" --stress)
add_executable(clutch_capture_tests unit/capture_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/platform/capture.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/arguments.cpp")
clutch_target_defaults(clutch_capture_tests)
add_test(NAME clutch.capture COMMAND clutch_capture_tests)
add_test(NAME clutch.stress-capture COMMAND clutch_capture_tests --stress)
set_tests_properties(clutch.source-files clutch.capture PROPERTIES LABELS "unit" TIMEOUT 30)
set_tests_properties(clutch.stress-source-files clutch.stress-capture PROPERTIES LABELS "stress" TIMEOUT 120)
set_tests_properties(clutch.version PROPERTIES
        PASS_REGULAR_EXPRESSION "clutch ${CLUTCH_VERSION}")

# Verify that static MSVC builds start without compiler DLL search paths.
if (WIN32 AND MSVC)
    find_program(CLUTCH_POWERSHELL NAMES powershell pwsh)
    if (CLUTCH_POWERSHELL)
        add_test(NAME clutch.standalone
                COMMAND "${CLUTCH_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/integration/windows-standalone.ps1"
                -Clutch "$<TARGET_FILE:clutch>"
                -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}/standalone-tests")
    endif ()
endif ()

# Graph safety checks run without Graphviz. When installed (as in the
# development container), also parse and render the real DOT output as SVG.
add_executable(clutch_graph_tests
        unit/graph_tests.cpp
        "${PROJECT_SOURCE_DIR}/src/core/warnings/policy.cpp"
        "${PROJECT_SOURCE_DIR}/src/toolchains/safety.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/capture.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/build_graph.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/dependencies/dependency.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/pch/pch.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/targets/resolve.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/targets/requirements.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/targets/target.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/targets/runtime.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/graph_visualization.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/files.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/arguments.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/environment.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/process.cpp")
clutch_target_defaults(clutch_graph_tests)
find_program(CLUTCH_GRAPHVIZ NAMES dot)
if (CLUTCH_GRAPHVIZ)
    add_test(NAME clutch.graph
            COMMAND clutch_graph_tests "${CMAKE_CURRENT_BINARY_DIR}/graph-tests" "${CLUTCH_GRAPHVIZ}")
else ()
    add_test(NAME clutch.graph
            COMMAND clutch_graph_tests "${CMAKE_CURRENT_BINARY_DIR}/graph-tests")
endif ()

# An explicit Ninja path opts Windows into compiler-backed integration checks.
# MSVC handles both languages; GNU/Clang need the matching C compiler driver.
set(CLUTCH_TEST_NINJA "" CACHE FILEPATH "Ninja executable for integration tests")
if (WIN32 AND CLUTCH_TEST_NINJA)
    if (MSVC)
        set(CLUTCH_TEST_CC "${CMAKE_CXX_COMPILER}")
    else ()
        get_filename_component(CLUTCH_TEST_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(CLUTCH_TEST_C_DRIVER gcc)
        else ()
            set(CLUTCH_TEST_C_DRIVER clang)
        endif ()
        find_program(CLUTCH_TEST_CC NAMES "${CLUTCH_TEST_C_DRIVER}"
                HINTS "${CLUTCH_TEST_COMPILER_DIRECTORY}" REQUIRED)
    endif ()

    # Discovered PowerShell executable is cached by find_program for reuse.
    find_program(CLUTCH_POWERSHELL powershell REQUIRED)
    add_test(NAME clutch.integration
            COMMAND "${CLUTCH_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/integration/windows.ps1"
            -Clutch "$<TARGET_FILE:clutch>" -Ninja "${CLUTCH_TEST_NINJA}"
            -CC "${CLUTCH_TEST_CC}" -CXX "${CMAKE_CXX_COMPILER}")
endif ()

# The POSIX suite validates both independent generators with real C/C++ tools.
# This bootstrap project only enables CXX, so discover a C compiler explicitly.
if (UNIX)
    find_program(CLUTCH_TEST_BASH NAMES bash)
    find_program(CLUTCH_TEST_POSIX_NINJA NAMES ninja ninja-build)
    find_program(CLUTCH_TEST_MAKE NAMES gmake make)
    find_program(CLUTCH_TEST_CC NAMES cc clang gcc)
    if (CLUTCH_TEST_BASH AND CLUTCH_TEST_POSIX_NINJA AND CLUTCH_TEST_MAKE AND CLUTCH_TEST_CC)
        add_test(NAME clutch.posix-integration
                COMMAND "${CLUTCH_TEST_BASH}"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/posix.sh"
                "$<TARGET_FILE:clutch>" "${CLUTCH_TEST_POSIX_NINJA}"
                "${CLUTCH_TEST_MAKE}" "${CLUTCH_TEST_CC}" "${CMAKE_CXX_COMPILER}")
    endif ()
endif ()

# Verify the public protocol with an independent JSON reader and real tools.
# Python is only a test dependency; the installed executable remains native.
find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
if (CLUTCH_TEST_NINJA)
    set(CLUTCH_API_NINJA "${CLUTCH_TEST_NINJA}")
elseif (CMAKE_GENERATOR MATCHES "^Ninja")
    set(CLUTCH_API_NINJA "${CMAKE_MAKE_PROGRAM}")
else ()
    find_program(CLUTCH_API_NINJA NAMES ninja ninja-build)
endif ()
if (MSVC)
    set(CLUTCH_API_CC "${CMAKE_CXX_COMPILER}")
elseif (CLUTCH_TEST_CC)
    set(CLUTCH_API_CC "${CLUTCH_TEST_CC}")
else ()
    find_program(CLUTCH_API_CC NAMES cc clang gcc)
endif ()
if (CLUTCH_TEST_SELF_HOSTING AND NOT (Python3_Interpreter_FOUND AND CLUTCH_API_NINJA AND CLUTCH_API_CC))
    message(FATAL_ERROR "Self-hosting verification requires Python 3.9+, Ninja and a C compiler")
endif ()
if (Python3_Interpreter_FOUND AND CLUTCH_API_NINJA AND CLUTCH_API_CC)
    # Opt in explicitly: this compiles the complete tool twice, independently of
    # the short functional suites used in ordinary edit/test cycles.
    if (CLUTCH_TEST_SELF_HOSTING)
        add_test(NAME clutch.self-hosting
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/self_hosting.py"
                --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
                --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}"
                --work-root "${CMAKE_CURRENT_BINARY_DIR}/self-hosting")
        set_tests_properties(clutch.self-hosting PROPERTIES LABELS "self-hosting" TIMEOUT 1800)
    endif ()
    add_test(NAME clutch.tooling-safety
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/tooling_safety_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.tooling-safety PROPERTIES TIMEOUT 180)
    # Script commands are explicit user tools, with lifecycle/failure gates.
    add_test(NAME clutch.commands
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/commands_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.commands PROPERTIES TIMEOUT 180)
    # Live processes use bounded rendezvous/cleanup, not timestamp assumptions.
    add_test(NAME clutch.watch
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/watch_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.watch PROPERTIES TIMEOUT 240)
    # Compile shipped feature examples unchanged, including relocated source bundles.
    add_test(NAME clutch.feature-examples
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/feature_examples_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.feature-examples PROPERTIES TIMEOUT 240)
    add_test(NAME clutch.configured-files
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/configured_files_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.configured-files PROPERTIES TIMEOUT 180)
    # Exercise omitted settings with real native tools and isolated availability.
    add_test(NAME clutch.defaults
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/defaults_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.defaults PROPERTIES TIMEOUT 240)
    # Python independently checks ZIP headers/CRCs and rebuilds relocated sources.
    add_test(NAME clutch.bundle
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/bundle_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.bundle PROPERTIES TIMEOUT 240)
    add_test(NAME clutch.stress-integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/stress_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    set_tests_properties(clutch.stress-integration PROPERTIES LABELS "stress" TIMEOUT 240)
    find_program(CLUTCH_TEST_CMAKE NAMES cmake)
    if (CLUTCH_TEST_CMAKE)
        add_test(NAME clutch.cmake-projects
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/cmake_project_integration.py"
                --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
                --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
        set_tests_properties(clutch.cmake-projects PROPERTIES TIMEOUT 240)
        add_test(NAME clutch.foreign-dependencies
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/foreign_dependencies_integration.py"
                --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
                --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    endif ()
    add_test(NAME clutch.file-api
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/file_api_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    add_test(NAME clutch.targets
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/targets_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    add_test(NAME clutch.workspace
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/workspace_integration.py"
            --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
            --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    find_program(CLUTCH_TEST_GIT NAMES git)
    if (CLUTCH_TEST_GIT)
        # Shipped package examples run offline by default. Real GitHub/CUDA
        # smoke checks are explicit options on the same Python test runner.
        if (CLUTCH_TEST_CMAKE)
            add_test(NAME clutch.package-examples
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/package_examples_integration.py"
                    --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
                    --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
        endif ()
        add_test(NAME clutch.dependencies
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/dependencies_integration.py"
                --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
                --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    endif ()
endif ()
