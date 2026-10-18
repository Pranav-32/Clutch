# Bootstrap and develop Clutch with CMake; generated Clutch projects use their
# selected backend directly and do not depend on this bootstrap project.
cmake_minimum_required(VERSION 3.20)

# Numeric project metadata and Git-aware CLI identity share the root VERSION.
include(cmake/ClutchVersion.cmake)
clutch_read_version("${CMAKE_CURRENT_SOURCE_DIR}" CLUTCH_VERSION)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" CLUTCH_VERSION_CORE "${CLUTCH_VERSION}")
project(Clutch VERSION "${CLUTCH_VERSION_CORE}" LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
clutch_generate_version("${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

# A commit, new tag or dirty edit must update --version without a manual
# reconfiguration. Changed-content writes keep unchanged builds incremental.
add_custom_target(clutch_version
  COMMAND "${CMAKE_COMMAND}"
    "-DCLUTCH_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DCLUTCH_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.hpp"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.txt"
  VERBATIM)

# Build one native CLI executable. Vendored dependencies are header-only.
add_executable(clutch
  src/main.cpp
  src/app/cli.cpp
  src/app/workflow.cpp
  src/backends/backend.cpp
  src/backends/make/make.cpp
  src/backends/ninja/ninja.cpp
  src/backends/visual_studio/visual_studio.cpp
  src/backends/xcode/xcode.cpp
  src/core/build_directory.cpp
  src/core/build_graph.cpp
  src/core/compilation_database.cpp
  src/core/configure.cpp
  src/core/configuration_store.cpp
  src/core/file_api/file_api.cpp
  src/core/file_api/model.cpp
  src/core/file_api/paths.cpp
  src/core/file_api/protocol.cpp
  src/core/file_api/query.cpp
  src/core/graph_visualization.cpp
  src/core/project.cpp
  src/core/targets/archive.cpp
  src/core/targets/manifest.cpp
  src/core/targets/resolve.cpp
  src/core/targets/runtime.cpp
  src/core/targets/target.cpp
  src/core/workspace/inputs.cpp
  src/core/workspace/lock.cpp
  src/core/workspace/workspace.cpp
  src/platform/files.cpp
  src/platform/json.cpp
  src/platform/process.cpp
  src/platform/terminal.cpp
  src/toolchains/toolchain.cpp
)
add_dependencies(clutch clutch_version)

# Keep executable policies consistent as more test/tool targets are introduced.
# C++17 is Clutch's baseline, independent of a user's project standard.
function(clutch_target_defaults target)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

  # Static MSVC CRT permits distribution without a VC redistributable install.
  if(MSVC)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

clutch_target_defaults(clutch)
# Internal module headers are co-located with their sources; src/ anchors
# cross-module includes. Treat unchanged upstream headers as system dependencies.
target_include_directories(clutch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(clutch SYSTEM PRIVATE third_party)

# Standard installation places the executable under the selected prefix's bin/.
install(TARGETS clutch RUNTIME DESTINATION bin)

# Ship Clutch and incorporated header-library notices with installed binaries.
install(FILES
  LICENSE
  third_party/CLI11.LICENSE
  third_party/tomlplusplus.LICENSE
  third_party/nlohmann.LICENSE
  DESTINATION share/licenses/clutch)

# CTest exposes smoke checks by default; BUILD_TESTING=OFF disables registration.
include(CTest)
if(BUILD_TESTING)
  add_test(NAME clutch.help COMMAND clutch --help)
  add_test(NAME clutch.version COMMAND clutch --version)
  set_tests_properties(clutch.version PROPERTIES
    PASS_REGULAR_EXPRESSION "clutch ${CLUTCH_VERSION}")

  # Verify that static MSVC builds start without compiler DLL search paths.
  if(WIN32 AND MSVC)
    find_program(CLUTCH_POWERSHELL NAMES powershell pwsh)
    if(CLUTCH_POWERSHELL)
      add_test(NAME clutch.standalone
        COMMAND "${CLUTCH_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
          -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows-standalone.ps1"
          -Clutch "$<TARGET_FILE:clutch>"
          -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}/standalone-tests")
    endif()
  endif()

  # Graph safety checks run without Graphviz. When installed (as in the
  # development container), also parse and render the real DOT output as SVG.
  add_executable(clutch_graph_tests
    tests/graph_tests.cpp
    src/core/build_graph.cpp
    src/core/targets/resolve.cpp
    src/core/targets/target.cpp
    src/core/graph_visualization.cpp
    src/platform/files.cpp
    src/platform/process.cpp)
  clutch_target_defaults(clutch_graph_tests)
  find_program(CLUTCH_GRAPHVIZ NAMES dot)
  if(CLUTCH_GRAPHVIZ)
    add_test(NAME clutch.graph
      COMMAND clutch_graph_tests "${CMAKE_CURRENT_BINARY_DIR}/graph-tests" "${CLUTCH_GRAPHVIZ}")
  else()
    add_test(NAME clutch.graph
      COMMAND clutch_graph_tests "${CMAKE_CURRENT_BINARY_DIR}/graph-tests")
  endif()

  # An explicit Ninja path opts Windows into compiler-backed integration checks.
  # MSVC handles both languages; GNU/Clang need the matching C compiler driver.
  set(CLUTCH_TEST_NINJA "" CACHE FILEPATH "Ninja executable for integration tests")
  if(WIN32 AND CLUTCH_TEST_NINJA)
    if(MSVC)
      set(CLUTCH_TEST_CC "${CMAKE_CXX_COMPILER}")
    else()
      get_filename_component(CLUTCH_TEST_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
      if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CLUTCH_TEST_C_DRIVER gcc)
      else()
        set(CLUTCH_TEST_C_DRIVER clang)
      endif()
      find_program(CLUTCH_TEST_CC NAMES "${CLUTCH_TEST_C_DRIVER}"
        HINTS "${CLUTCH_TEST_COMPILER_DIRECTORY}" REQUIRED)
    endif()

    # Discovered PowerShell executable is cached by find_program for reuse.
    find_program(CLUTCH_POWERSHELL powershell REQUIRED)
    add_test(NAME clutch.integration
      COMMAND "${CLUTCH_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.ps1"
        -Clutch "$<TARGET_FILE:clutch>" -Ninja "${CLUTCH_TEST_NINJA}"
        -CC "${CLUTCH_TEST_CC}" -CXX "${CMAKE_CXX_COMPILER}")
  endif()

  # The POSIX suite validates both independent generators with real C/C++ tools.
  # This bootstrap project only enables CXX, so discover a C compiler explicitly.
  if(UNIX)
    find_program(CLUTCH_TEST_BASH NAMES bash)
    find_program(CLUTCH_TEST_POSIX_NINJA NAMES ninja ninja-build)
    find_program(CLUTCH_TEST_MAKE NAMES gmake make)
    find_program(CLUTCH_TEST_CC NAMES cc clang gcc)
    if(CLUTCH_TEST_BASH AND CLUTCH_TEST_POSIX_NINJA AND CLUTCH_TEST_MAKE AND CLUTCH_TEST_CC)
      add_test(NAME clutch.posix-integration
        COMMAND "${CLUTCH_TEST_BASH}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/posix-integration.sh"
          "$<TARGET_FILE:clutch>" "${CLUTCH_TEST_POSIX_NINJA}"
          "${CLUTCH_TEST_MAKE}" "${CLUTCH_TEST_CC}" "${CMAKE_CXX_COMPILER}")
    endif()
  endif()

  # Verify the public protocol with an independent JSON reader and real tools.
  # Python is only a test dependency; the installed executable remains native.
  find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
  if(CLUTCH_TEST_NINJA)
    set(CLUTCH_API_NINJA "${CLUTCH_TEST_NINJA}")
  elseif(CMAKE_GENERATOR MATCHES "^Ninja")
    set(CLUTCH_API_NINJA "${CMAKE_MAKE_PROGRAM}")
  else()
    find_program(CLUTCH_API_NINJA NAMES ninja ninja-build)
  endif()
  if(MSVC)
    set(CLUTCH_API_CC "${CMAKE_CXX_COMPILER}")
  elseif(CLUTCH_TEST_CC)
    set(CLUTCH_API_CC "${CLUTCH_TEST_CC}")
  else()
    find_program(CLUTCH_API_CC NAMES cc clang gcc)
  endif()
  if(Python3_Interpreter_FOUND AND CLUTCH_API_NINJA AND CLUTCH_API_CC)
    add_test(NAME clutch.file-api
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/file_api_integration.py"
        --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
        --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    add_test(NAME clutch.targets
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/targets_integration.py"
        --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
        --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
    add_test(NAME clutch.workspace
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_integration.py"
        --clutch "$<TARGET_FILE:clutch>" --cc "${CLUTCH_API_CC}"
        --cxx "${CMAKE_CXX_COMPILER}" --ninja "${CLUTCH_API_NINJA}" -v)
  endif()
endif()
