# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
#
# Register native tests by purpose. CTest names remain stable across platforms;
# compiler-backed suites require their actual host tools and a built Clutch.

add_test(NAME clutch.help COMMAND clutch --help)
add_test(NAME clutch.version COMMAND clutch --version)
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
        "${PROJECT_SOURCE_DIR}/src/core/build_graph.cpp"
        "${PROJECT_SOURCE_DIR}/src/core/targets/resolve.cpp"
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
if (Python3_Interpreter_FOUND AND CLUTCH_API_NINJA AND CLUTCH_API_CC)
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
endif ()
