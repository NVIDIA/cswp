# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# ------------------------------------------------
# Build client cstrace_client shared library
# ------------------------------------------------
set (src
  ./cstrace_base.cpp
  ./cstrace_usb.cpp
  ./cstrace_plat_if.cpp
)

# Shared library
add_library(cstrace_client SHARED ${src})
set_property(TARGET cstrace_client PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(cstrace_client PUBLIC
  plat_usb_if
  ${CMAKE_DL_LIBS})
target_include_directories(cstrace_client PUBLIC
  ../common
  include)

# Static library
add_library(cstrace_client_static STATIC ${src})
target_link_libraries(cstrace_client_static PUBLIC
  plat_usb_if
  ${CMAKE_DL_LIBS})
target_include_directories(cstrace_client_static PUBLIC
  ../common
  include)

# Release cstrace_client shared lib
release_library(cstrace_client)
release_library(cstrace_client_static)

# Release include files
release_header(include/cstrace_types.h)
release_header(include/cstrace_plat_if.h)
release_header(include/cstrace_usb.h)
release_header(include/cstrace_base.h)
