# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Optional GNU-driver build: run from the adapter's isolated BUILD_DIR.
empty :=
space := $(empty) $(empty)
# Escape spaces only in prerequisites; quote actual compiler argument paths.
source := $(subst $(space),\$(space),$(SOURCE_DIR))
AR ?= ar

.PHONY: all
all: libnumber.a

number.o: $(source)/src/number.cpp $(source)/src/number.hpp
	"$(CXX)" -std=c++17 $(CXXFLAGS) -c "$(SOURCE_DIR)/src/number.cpp" -o "$@"

libnumber.a: number.o
	"$(AR)" rcs "$@" "$<"
