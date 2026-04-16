#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test for device 0 memory setmem commands

# Set and clear log file
LOG_FILE="logs/dev0/dev0_setmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev0_mem 0x810004 4"
execute_and_log "ccu set_dev0_mem 0x810004 4 0x2f99ff"
execute_and_log "ccu get_dev0_mem 0x810004 4"
execute_and_log "ccu set_dev0_mem 0x810004 4 0x2fffff" #reset value for next time
execute_and_log "ccu get_dev0_mem 0x810004 4"
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu set_dev0_mem 0x1 4 0xffffffff"    # unaligned address
execute_and_log "ccu set_dev0_mem 0x3 4 0xffffffff"    # unaligned address
execute_and_log "ccu set_dev0_mem 0x810002 4 0x2fffff" # unaligned address
execute_and_log "ccu set_dev0_mem 0x0 2 0xffff"        # size < 4
execute_and_log "ccu set_dev0_mem 0x810004 8 0x2fccff" # size > 4
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
