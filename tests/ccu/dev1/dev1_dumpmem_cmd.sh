#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test for device 1 memory dumpmem commands

# Set and clear log file
LOG_FILE="logs/dev1/dev1_dumpmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu dump_dev1_mem 0xc0000000 399"
check_and_rename_file "mem_dump.bin" "dev1_mem_dump_0.bin"
execute_and_log "ccu dump_dev1_mem 0xc0000010 809"
check_and_rename_file "mem_dump.bin" "dev1_mem_dump_1.bin"
execute_and_log "ccu dump_dev1_mem 0xc0000018 18311"
check_and_rename_file "mem_dump.bin" "dev1_mem_dump_2.bin"
execute_and_log "ccu dump_dev1_mem 0xc0000020 262144"
check_and_rename_file "mem_dump.bin" "dev1_mem_dump_3.bin"
# Global mem2
# Global IO
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu dump_dev1_mem 0xc0000014 260000" # unAaligned address
execute_and_log "ccu dump_dev1_mem 0xc0000000 262244" # Exceeds dump limit of 256kB
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
