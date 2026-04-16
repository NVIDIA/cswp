#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test for device 1 memory getmem commands

# Set and clear log file
LOG_FILE="logs/dev1/dev1_getmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
# Global mem CPU package0 mem
execute_and_log "ccu get_dev1_mem 0xc0000000 1"   # read 1 byte
execute_and_log "ccu get_dev1_mem 0xc0000001 1"   # read 1 byte
execute_and_log "ccu get_dev1_mem 0xc0000002 1"   # read 1 byte
execute_and_log "ccu get_dev1_mem 0xc0000003 1"   # read 1 byte
execute_and_log "ccu get_dev1_mem 0xc0000007 1"   # read 1 byte
execute_and_log "ccu get_dev1_mem 0xc0000002 2"   # read 2 byte
execute_and_log "ccu get_dev1_mem 0xc0000006 2"   # read 2 bytes
execute_and_log "ccu get_dev1_mem 0xc0000004 3"   # read 3 bytes
execute_and_log "ccu get_dev1_mem 0xc0000008 4"   # read 4 bytes
execute_and_log "ccu get_dev1_mem 0xc0000010 7"   # read 7 bytes
execute_and_log "ccu get_dev1_mem 0xc0000018 400" # read 400 bytes
execute_and_log "ccu get_dev1_mem 0xc0000000 151" # read 151 bytes
# Global mem2
#execute_and_log "ccu get_dev1_mem 0x2000000080000 8" # read 400 bytes # TODO: RAS error
# Global IO
execute_and_log "ccu get_dev1_mem 0xFE1476000000 4" # read 4 bytes Rom entry for core0. should read 0x10007
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu get_dev1_mem 0xc0000001 2"   # read from unaligned addr
execute_and_log "ccu get_dev1_mem 0xc0000005 4"   # read from unaligned addr
execute_and_log "ccu get_dev1_mem 0xc0000003 3"   # read from unaligned addr
execute_and_log "ccu get_dev1_mem 0xc0000004 8"   # read from unaligned addr
execute_and_log "ccu get_dev1_mem 0xc0000000 401" # exceeds max size
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
