/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#ifndef CCU_IN_H
#define CCU_IN_H

#include "cswp_types.h"
#include "cstrace_types.h"
#include "cstrace_base.h"

/*
 * ----------------------------------------------------------------------------
 * Device commands related information
 * ----------------------------------------------------------------------------
 */
#define CCU_3LCMD_DEVCHAR_POS 7
#define CCU_4LCMD_DEVCHAR_POS 8
#define MAX_ASYNC_MSG_LEN     1024 // 1kB limit enforced by server
typedef struct {
    uint32_t devID;
} dev_cmd_t;

/*
 * ----------------------------------------------------------------------------
 * Register commands related information
 * ----------------------------------------------------------------------------
 */
#define DEV0_REG_COUNT       4
#define MAX_REG_INFO_ARR_SZ  7200 // To allow name,display and description of 4 registers
typedef struct {
    uint32_t devID;
    uint64_t regID;
    size_t   regSZ; // 1: 32b 2:64b
    uint64_t value;
} reg_cmd_t;

/*
 * ----------------------------------------------------------------------------
 * Memory commands related information
 * ----------------------------------------------------------------------------
 */
// Whether its a batch request/response or single single request/response,
// the size cannot exceed CSWP_REQ_RSP_BUFFER_SIZE - 32 (allocated for some
// server information).
#define MEM_MAX_RW_SZ    16352
#define MAX_NUM_SUBREQ   4        // worse case req is (8*n byte)+ 4 byte + 2 byte + 1 byte = 4
typedef struct {
    uint64_t           saddr;     // starting address for subreq
    cswp_access_size_t acc_sz;    // access size encoding of subreq
    uint64_t           size;      // size of subreq
    uint64_t           buf_idx;   // rw_values starting index for subreq
} subreq_info;

// Max size used for dumping read data to file or writing data to memory
#define DEV0_MEM_MAX_BIN_SZ   4096   // 4kB
#define DEV1_MEM_MAX_BIN_SZ   262144 // 256KB
#define MEM_MAX_BIN_SZ        DEV1_MEM_MAX_BIN_SZ
typedef struct {
    uint32_t           devID;
    uint64_t           start_addr;
    uint64_t           total_bytes;
    uint8_t            rw_values[MEM_MAX_BIN_SZ]; // Used to read or write bytes
    uint64_t           num_subreqs;
    subreq_info        subreqs[MAX_NUM_SUBREQ];
    uint64_t           flags;
} mem_cmd_t;

/*
 * ----------------------------------------------------------------------------
 * Streaming trace related information
 * ----------------------------------------------------------------------------
 */
#define FLUSH_TIMEOUT (10 * 1000)
typedef struct {
    int                 etrID;             // ID of the sink to collect data from
    size_t              num_Dbufs;         // Number of data buffers
    size_t              num_Ebufs;         // Number of event buffers
    size_t              num_tbufs;         // Number of total data+event buffers
    CSTraceEventBuffer* tbufs_info;        // Client buffer information array
    int*                tbufs_req_tokens;  // Client token information array
    size_t              num_pending_bufs;  // Number of pending buffers submitted by client to gather data and events.
    size_t              num_capture_bytes; // Number of bytes captured so far
    std::unique_ptr<StreamingTraceBase> tracep;
} cstrace_cmd_t;

/*
 * ----------------------------------------------------------------------------
 * CCU command line paramter extraction function declarations
 * ----------------------------------------------------------------------------
 */
bool extract_get_dev_cap_param (int argc, char* argv[], dev_cmd_t* d);
bool extract_get_dev_reg_param (int argc, char* argv[], reg_cmd_t* r);
bool extract_set_dev_reg_param (int argc, char* argv[], reg_cmd_t* r);
bool extract_get_dev_mem_param (int argc, char* argv[], mem_cmd_t* m);
bool extract_dump_dev_mem_param(int argc, char* argv[], mem_cmd_t* m);
bool extract_set_dev_mem_param (int argc, char* argv[], mem_cmd_t* m);
bool extract_fill_dev_mem_param(int argc, char* argv[], mem_cmd_t* m);
bool extract_perf_cstrace_param(int argc, char* argv[], cstrace_cmd_t* c);

#endif /* CCU_IN_H */