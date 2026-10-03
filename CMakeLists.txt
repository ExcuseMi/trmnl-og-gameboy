# Game Boy emulator for the TRMNL OG. Build: make build (see README.md). SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(trmnl-og-gameboy)
