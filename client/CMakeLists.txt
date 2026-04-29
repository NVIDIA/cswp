# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

add_subdirectory(plat_usb_if)
add_subdirectory(cdebug)
add_subdirectory(cstrace)
# Needed for the release macros to work properly
set(INSTALLED_FILES ${INSTALLED_FILES} PARENT_SCOPE)
release_header(common/transport_cfg.h)
