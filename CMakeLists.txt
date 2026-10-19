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
    src/core/dependencies/dependency.cpp
    src/core/dependencies/cmake.cpp
    src/core/dependencies/foreign.cpp
    src/core/dependencies/make.cpp
    src/core/dependencies/git.cpp
    src/core/dependencies/manifest.cpp
    src/core/dependencies/resolve.cpp
    src/core/dependencies/sources.cpp
    src/core/dependencies/system.cpp
    src/core/dependencies/vcpkg.cpp
    src/core/dependencies/version.cpp
    src/core/file_api/file_api.cpp
    src/core/file_api/model.cpp
    src/core/file_api/paths.cpp
    src/core/file_api/protocol.cpp
    src/core/file_api/query.cpp
    src/core/graph_visualization.cpp
    src/core/pch/headers.cpp
    src/core/pch/pch.cpp
    src/core/project.cpp
    src/core/source_files.cpp
    src/core/targets/archive.cpp
    src/core/targets/manifest.cpp
    src/core/targets/requirements.cpp
    src/core/targets/resolve.cpp
    src/core/targets/runtime.cpp
    src/core/targets/target.cpp
    src/core/workspace/inputs.cpp
    src/core/workspace/lock.cpp
    src/core/workspace/workspace.cpp
    src/platform/arguments.cpp
    src/platform/capture.cpp
    src/platform/environment.cpp
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
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")

    # Static MSVC CRT permits distribution without a VC redistributable install.
    if (MSVC)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
endfunction()

clutch_target_defaults(clutch)
if (WIN32)
    # CLI11 normalizes the native wide command line with CommandLineToArgvW.
    target_link_libraries(clutch PRIVATE shell32)
endif ()

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
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
