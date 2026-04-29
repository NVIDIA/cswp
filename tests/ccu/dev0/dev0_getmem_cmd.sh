#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test for device 0 memory getmem commands

# Set and clear log file
LOG_FILE="logs/dev0/dev0_getmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev0_mem 0x0 1"   # read 1 byte
execute_and_log "ccu get_dev0_mem 0x4 2"   # read 2 bytes
execute_and_log "ccu get_dev0_mem 0x8 3"   # read 3 bytes
execute_and_log "ccu get_dev0_mem 0x1c 4"  # read 4 bytes
execute_and_log "ccu get_dev0_mem 0x68 8"  # read 8 bytes
execute_and_log "ccu get_dev0_mem 0x66000000 4" # read 4 bytes Rom entry for core0. should read 0x10007
execute_and_log "ccu get_dev0_mem 0x0 151" # read 151 bytes
execute_and_log "ccu get_dev0_mem 0x0 400" # read 400 bytes
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu get_dev0_mem 0x1 1"   # read from unaligned addr
execute_and_log "ccu get_dev0_mem 0x2 4"   # read from unaligned addr
execute_and_log "ccu get_dev0_mem 0x3 3"   # read from unaligned addr
execute_and_log "ccu get_dev0_mem 0x0 401" # exceeds max size
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
