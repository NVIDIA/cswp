/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#ifndef CCU_OUT_H
#define CCU_OUT_H

#include "cswp_types.h"

// Function declarations
void process_init_cswp_out    (uint64_t serverProtoV, const char* serverID, uint32_t serverV);
void process_term_cswp_out    (void);
void process_dev_open_out     (char** deviceInfoList, uint32_t deviceCount);
void process_dev_close_out    (void);
void process_get_dev_info_out (char** deviceList, char** deviceTypes, uint32_t deviceCount);
void process_get_sys_info_out (unsigned descriptionFormat, unsigned descriptionSize, const uint8_t* descriptionDataBuffer);
void process_get_dev_cap_out  (uint32_t capabilities, uint32_t capabilityData);
void process_get_reg_list_out (uint32_t registerCount, cswp_register_info_t * registerInfo);
void process_get_dev_reg_out  (uint32_t* registerValue, size_t registerValueSize);
void process_set_dev_reg_out  (void);
void process_get_dev_mem_out  (uint64_t address, uint8_t * buf, size_t bytesRead);
void process_dump_dev_mem_out (uint8_t * buf, size_t bytesRead);
void process_set_dev_mem_out  (void);
void process_async_message_out(uint64_t deviceNo, uint64_t msg_level, char * msg);

#endif /* CCU_OUT_H */