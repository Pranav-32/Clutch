# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Build-time entry point; using CMake keeps Git metadata independent of Python.
if (NOT DEFINED CLUTCH_SOURCE_DIR OR NOT DEFINED CLUTCH_BINARY_DIR)
    message(FATAL_ERROR "CLUTCH_SOURCE_DIR and CLUTCH_BINARY_DIR are required")
endif ()
include("${CMAKE_CURRENT_LIST_DIR}/ClutchVersion.cmake")
clutch_generate_version("${CLUTCH_SOURCE_DIR}" "${CLUTCH_BINARY_DIR}")
