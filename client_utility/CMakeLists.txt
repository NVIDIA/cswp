# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# ------------------------------------------------
# Create a shared library and executable
# ------------------------------------------------
set(src
  ./ccu_in.cpp
  ./ccu_out.cpp
  ./ccu_cswp.cpp
  ./main.cpp
)

# This indicate where release_files put the config files relative to ccu
add_definitions("-DCONFIG_FILES_PATH=\"../share/${PROJECT_NAME}\"")
add_executable(ccu ${src})
target_link_libraries(ccu PRIVATE
  cdebug_client_static
  cstrace_client_static
  usb-1.0
  pthread)
target_include_directories(ccu PRIVATE
  include)

# Release the ccu executable
release_program(ccu)

# -------------------------------------------------
# Create a dummy server to test ccu over TCP socket
# -------------------------------------------------
set(src ./dummy_server_main.cpp)

add_executable(ccu_dummy_server ${src})

# Release the ccu_dummy_server executable
release_program(ccu_dummy_server)

# ------------------------------------------------
# Release trasnport cfg file
# ------------------------------------------------
# Release debug connection configuration file for TCP socket
release_file(${CMAKE_CURRENT_SOURCE_DIR}/cdebug_trnsprt_cfg.txt)
release_file(${CMAKE_CURRENT_SOURCE_DIR}/cstrace_trnsprt_cfg.txt)
