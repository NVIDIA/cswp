#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.md for the full license.

# Test for device 0 register getreg commands

# Set and clear log file
LOG_FILE="logs/dev0/dev0_getreg.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev0_reg 1 0xd00" #CSW
execute_and_log "ccu get_dev0_reg 1 0xDf8" #BASE
execute_and_log "ccu get_dev0_reg 1 0xdFC" #IDR
execute_and_log "ccu get_dev0_reg 1 0x4e560000" #ASYNC_MSG
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu get_dev0_reg 1 0xDf4" # Invalid regID
execute_and_log "ccu get_dev0_reg 2 0xd00" # Invalid regSize
execute_and_log "ccu get_dev0_reg 2 0xdf8" # Invalid regSize
execute_and_log "ccu get_dev0_reg 2 0xdfc" # Invalid regSize
execute_and_log "ccu get_dev0_reg 2 0x4e560000" # Invalid regSize
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
