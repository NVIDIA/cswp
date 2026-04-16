#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test for device 0 register setreg commands

# Set and clear log file
LOG_FILE="logs/dev0/dev0_setreg.log"
> "$LOG_FILE"

source tests/utility.sh

# START server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev0_reg 1 0x4e560000"         # ASYNC_MSG read before write
execute_and_log "ccu set_dev0_reg 1 0x4e560000 0x10001" # ASYNC_MSG
execute_and_log "ccu get_dev0_reg 1 0x4e560000"         # ASYNC_MSG read back
# The immedite reset write is needed to avoid generating interleaving of
# async responses with following commands
execute_and_log "ccu set_dev0_reg 1 0x4e560000 0x0"     # reset ASYNC_MSG
execute_and_log "ccu get_dev0_reg 1 0x4e560000"         # ASYNC_MSG read back
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu set_dev0_reg 1 0x4e560060 0x10001" # Invalid regID
execute_and_log "ccu set_dev0_reg 2 0x4e560000 0x10001" # Invalid regSize
execute_and_log "ccu set_dev0_reg 1 0xd00 0x10001" # Unsupported op
execute_and_log "ccu set_dev0_reg 1 0xdf8 0x10001" # Unsupported op
execute_and_log "ccu set_dev0_reg 1 0xdfc 0x10001" # Unsupported op
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
