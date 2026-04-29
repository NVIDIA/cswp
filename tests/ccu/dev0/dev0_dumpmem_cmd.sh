#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test for device 0 memory dumpmem commands

# Set and clear log file
LOG_FILE="logs/dev0/dev0_dumpmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu dump_dev0_mem 0x0 399"
check_and_rename_file "mem_dump.bin" "dev0_mem_dump_0.bin"
execute_and_log "ccu dump_dev0_mem 0x0 4096"
check_and_rename_file "mem_dump.bin" "dev0_mem_dump_1.bin"
execute_and_log "ccu dump_dev0_mem 0x4 811"
check_and_rename_file "mem_dump.bin" "dev0_mem_dump_2.bin"
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu dump_dev0_mem 0x1 4096" # unAaligned address
execute_and_log "ccu dump_dev0_mem 0x2 4096" # unAaligned address
execute_and_log "ccu dump_dev0_mem 0x3 4096" # unAaligned address
execute_and_log "ccu dump_dev0_mem 0x0 4100" # Exceeds dump limit of 4kB
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
