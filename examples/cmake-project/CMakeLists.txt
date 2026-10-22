# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Keep this application as an original CMake project, including its custom rules.
cmake_minimum_required(VERSION 3.21)
project(ClutchCMakeExample VERSION 1.0.0 LANGUAGES CXX)

# Generate a public build input with CMake's dependency tracking and quoting.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/message.hpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/message.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/message.hpp"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/message.in"
    VERBATIM)

# Framework helpers can own target creation and product settings in the same way.
function(add_example_application name)
    add_executable(${name} main.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/message.hpp")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(${name} PRIVATE cxx_std_17)
    set_target_properties(${name} PROPERTIES OUTPUT_NAME "hello-cmake")
endfunction()
add_example_application(hello)

# Clutch test/install/package delegates these original CMake declarations.
enable_testing()
add_test(NAME hello COMMAND hello)
install(TARGETS hello RUNTIME DESTINATION bin)
set(CPACK_GENERATOR ZIP)
include(CPack)
