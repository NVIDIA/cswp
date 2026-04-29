#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test for device 1 memory setmem commands

# Set and clear log file
LOG_FILE="logs/dev1/dev1_setmem.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev1_mem 0xc0000000 8"
execute_and_log "ccu set_dev1_mem 0xc0000001 1 0x99"

execute_and_log "ccu get_dev1_mem 0xc0000000 8"
execute_and_log "ccu set_dev1_mem 0xc0000002 2 0x7799"

execute_and_log "ccu get_dev1_mem 0xc0000000 8"
execute_and_log "ccu set_dev1_mem 0xc0000004 4 0x11223344"

execute_and_log "ccu get_dev1_mem 0xc0000000 8"
execute_and_log "ccu set_dev1_mem 0xc0000000 8 0x99999999ffffffff"
execute_and_log "ccu get_dev1_mem 0xc0000000 8"

# Global mem2
execute_and_log "ccu get_dev1_mem 0x2000000000000 8"
execute_and_log "ccu set_dev1_mem 0x2000000000000 8 0x7777777777777777"
execute_and_log "ccu get_dev1_mem 0x2000000000000 8"

# Global IO
execute_and_log "ccu get_dev1_mem 0xfe158109040c 4"
execute_and_log "ccu set_dev1_mem 0xfe158109040c 4 0x77777777"
execute_and_log "ccu get_dev1_mem 0xfe158109040c 4"

execute_and_log "ccu get_dev1_mem 0xfe158109050c 4"
execute_and_log "ccu set_dev1_mem 0xfe158109050c 4 0x77777777"
execute_and_log "ccu get_dev1_mem 0xfe158109050c 4"
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu set_dev1_mem 0xc0000001 2 0xffff"               # unaligned address
execute_and_log "ccu set_dev1_mem 0xc0000002 4 0xfffffffff"          # unaligned address
execute_and_log "ccu set_dev1_mem 0xc0000004 8 0xffffffffffffffff"   # unaligned address
execute_and_log "ccu set_dev1_mem 0xc0000000 3 0xffffff"             # Invalid size
execute_and_log "ccu set_dev1_mem 0xc0000000 9 0x2fffffffffffffffff" # Invalid size >8
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
