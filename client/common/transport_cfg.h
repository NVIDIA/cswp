/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

// This header is used to define transport configuration data structures
// used to trasnfer data from higher level debug applications to CSWP platform
// interface library.

#ifndef TRANSPORT_CFG_H
#define TRANSPORT_CFG_H

#include <string>

//--------------------------------------------------------------------
// CSWP transport config data structure
//--------------------------------------------------------------------
typedef struct {
    std::string type; // usb/tcp
    // For USB transport
    int         vid;
    int         pid;
    int         iid;
    int         sid;
    std::string serial;
    // For TCP/socket transport
    std::string ipaddr;
    int         portid;
} transport_cfg_t;

#endif // TRANSPORT_CFG_H
