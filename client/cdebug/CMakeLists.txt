# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# ------------------------------------------------
# Build client cdebug shared library
# ------------------------------------------------
set(src
  ./cswp_client_common.c
  ./cswp_client_commands.c
  ./cswp_client_nv_ext_commands.c
  ./cswp_client_plat_if.cpp
  ./cdebug_c_wrapper.cpp
  ./cswp_client.c
  ./cswp_buffer.c
)

# Shared version
add_library(cdebug_client SHARED ${src})
set_property(TARGET cdebug_client PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(cdebug_client PUBLIC
  plat_usb_if
  ${CMAKE_DL_LIBS})
target_include_directories(cdebug_client PUBLIC
  ../common
  include)

# Static version
add_library(cdebug_client_static STATIC ${src})
target_link_libraries(cdebug_client_static PUBLIC
  plat_usb_if
  ${CMAKE_DL_LIBS})
target_include_directories(cdebug_client_static PUBLIC
  ../common
  include)

# Release cdebug_client shared lib
release_library(cdebug_client)
release_library(cdebug_client_static)

release_header(include/cswp_types.h)
release_header(include/cswp_client.h)
release_header(include/cswp_client_plat_if.h)
