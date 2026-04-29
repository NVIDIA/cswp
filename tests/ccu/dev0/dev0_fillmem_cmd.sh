#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test for device 0 memory fillmem commands

# Set and clear log file
LOG_FILE="logs/dev0/dev0_fillmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu dump_dev0_mem 0x0 4096"
execute_and_log "mv mem_dump.bin mem_dump.bin.old"
execute_and_log "ccu dump_dev0_mem 0x0 4096" # write are ignored as the area is ROM.
execute_and_log "diff mem_dump.bin mem_dump.bin.old"
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu fill_dev0_mem 0x2 test_bins/testload_4kB.bin"   # addr not aligned
execute_and_log "ccu fill_dev0_mem 0x0 test_bins/testload_7B.bin"    # bin size is not multiple of 4
execute_and_log "ccu fill_dev0_mem 0x0 test_bins/testload_280kB.bin" # bin size > 4kB
execute_and_log "ccu fill_dev0_mem 0x0 test_bins/testload_5kB.bin"   # bin not present
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
