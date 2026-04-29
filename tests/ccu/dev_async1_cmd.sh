#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test basic device related commands in CSWP

# Set and clear log file
LOG_FILE="logs/dev_async1_cmd.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
execute_and_log "ccu get_dev0_reg 1 0x4e560000"
execute_and_log "ccu set_dev0_reg 1 0x4e560000 0x10001"
execute_and_log "ccu get_dev0_reg 1 0x4e560000"

# Test interleaved async message
for i in {1..10}
do
    execute_and_log "ccu get_dev0_reg 1 0x4e560000"
done

# Needed since it doesn't reset when server is turned off.
execute_and_log "ccu set_dev0_reg 1 0x4e560000 0x0"
execute_and_log "ccu get_dev0_reg 1 0x4e560000"
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
