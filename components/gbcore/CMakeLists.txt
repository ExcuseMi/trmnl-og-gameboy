# Peanut-GB wrapper, 1-bit renderer and input interface. Hardware free: also built by tools/host.
# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS src/gb_core.c src/gb_render.c src/gb_save.c src/gb_state.c
                       INCLUDE_DIRS include ../../third_party/peanut_gb ../../include)
# make build GB_SHADE_MODE=threshold (default bayer), see gb_render.h
if(GB_SHADE_MODE STREQUAL "threshold")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GB_SHADE_MODE=1)
endif()
