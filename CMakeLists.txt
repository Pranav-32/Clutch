# Bootstrap and develop Clutch with CMake; generated Clutch projects use their
# selected backend directly and do not depend on this bootstrap project.
cmake_minimum_required(VERSION 3.20)
project(Clutch VERSION 0.1.0 LANGUAGES CXX)

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
  src/core/graph_visualization.cpp
  src/core/project.cpp
  src/platform/files.cpp
  src/platform/process.cpp
  src/platform/terminal.cpp
  src/toolchains/toolchain.cpp
)

# C++17 is the implementation baseline, independent of a user's project standard.
target_compile_features(clutch PRIVATE cxx_std_17)
# Internal module headers are co-located with their sources; src/ anchors
# cross-module includes. Treat unchanged upstream headers as system dependencies.
target_include_directories(clutch PRIVATE src)
target_include_directories(clutch SYSTEM PRIVATE third_party)

# Use a static MSVC runtime so distributed executables need no VC redistributable.
# Keep strict compiler diagnostics local to this target rather than global flags.
if(MSVC)
  set_property(TARGET clutch PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(clutch PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(clutch PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Standard installation places the executable under the selected prefix's bin/.
install(TARGETS clutch RUNTIME DESTINATION bin)

# Ship Clutch and incorporated header-library notices with installed binaries.
install(FILES
  LICENSE
  third_party/CLI11.LICENSE
  third_party/tomlplusplus.LICENSE
  DESTINATION share/licenses/clutch)

# CTest exposes smoke checks by default; BUILD_TESTING=OFF disables registration.
include(CTest)
if(BUILD_TESTING)
  add_test(NAME clutch.help COMMAND clutch --help)
  add_test(NAME clutch.version COMMAND clutch --version)

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
    src/core/graph_visualization.cpp
    src/platform/files.cpp
    src/platform/process.cpp)
  target_include_directories(clutch_graph_tests PRIVATE src)
  target_compile_features(clutch_graph_tests PRIVATE cxx_std_17)
  if(MSVC)
    set_property(TARGET clutch_graph_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(clutch_graph_tests PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(clutch_graph_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
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
endif()
