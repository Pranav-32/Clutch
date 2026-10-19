# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Model an existing SDK installation; configuring its consumer installs nothing.
set(ClutchDemoSdk_VERSION "1.0.0")

if (NOT TARGET ClutchDemoSdk::Headers)
    add_library(ClutchDemoSdk::Headers INTERFACE IMPORTED)
    set_target_properties(ClutchDemoSdk::Headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../headers")
endif ()
