#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test for device 1 memory fillmem commands

# Set and clear log file
LOG_FILE="logs/dev1/dev1_fillmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu dump_dev1_mem 0xc0000008 256"
execute_and_log "mv mem_dump.bin mem_dump.bin.old"
execute_and_log "ccu fill_dev1_mem 0xc0000008 test_bins/testload_256B.bin" # Contain all all 6 
execute_and_log "ccu dump_dev1_mem 0xc0000008 256"
execute_and_log "diff mem_dump.bin mem_dump.bin.old"

# Global mem2
# Global IO
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu fill_dev1_mem 0xc0000004 test_bins/testload_4kB.bin"   # addr not aligned
execute_and_log "ccu fill_dev1_mem 0xc0000000 test_bins/testload_280kB.bin" # bin size > 256kB
execute_and_log "ccu fill_dev1_mem 0xc0000000 test_bins/testload_5kB.bin"   # bin not present
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
