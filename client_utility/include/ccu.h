/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CCU_H
#define CCU_H

/* 0x08XX - CCU errors */
#define CCU_CFG_FILE_ERR           0x0800
#define CCU_CMD_NOT_SUPPORTED      0x0801
#define CCU_USB_RUNTIME_EXCEPTION  0x0802
#define CCU_UNKNOWN_EXCEPTION      0x0803
#define CCU_BAD_CMD_ARG            0x0804
#define CCU_FILEIO_ERR             0x0805
#define CCU_MALLOC_ERR             0x0806
#define CCU_SIG_HANDLE_ERR         0x0807
#define CCU_TBUF_NOT_FOUND_ERR     0x0808
#define CCU_BATCH_OPS_ERR          0x0809
#define CCU_ETR_END_OF_DATA        0x080a

// Function declarations
int init_cswp   (int argc, char* argv[]);
int term_cswp   (int argc, char* argv[]);
int get_dev_info(int argc, char* argv[]);
int get_dev_cap (int argc, char* argv[]);
int get_sys_info(int argc, char* argv[]);
int get_reg_list(int argc, char* argv[]);
int get_dev_reg (int argc, char* argv[]);
int set_dev_reg (int argc, char* argv[]);
int get_dev_mem (int argc, char* argv[]);
int dump_dev_mem(int argc, char* argv[]);
int set_dev_mem (int argc, char* argv[]);
int fill_dev_mem(int argc, char* argv[]);
int rd_async_msg(int argc, char* argv[]);
int perf_cstrace(int argc, char* argv[]);

typedef int (*ccufun_ptr) (int, char**);

#endif /* CCU_H */