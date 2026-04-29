# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

#-------------------------------------------------------
# Build plat_usb_if static lib
#-------------------------------------------------------
set(src 
  ./usb_device.cpp
  ./usb_device_linux.cpp # Linux platform only
)

add_library(plat_usb_if STATIC ${src})
set_property(TARGET plat_usb_if
  PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(plat_usb_if PRIVATE "-fvisibility=hidden")
target_include_directories(plat_usb_if PUBLIC
  include
  ${LIBUSB_INCLUDE_DIRS})
target_link_libraries(plat_usb_if PUBLIC usb-1.0)
   
# Release the lib
release_library(plat_usb_if)

# Release include files
release_header(include/usb_device.h)
release_header(include/plat_sync_prim.h)
