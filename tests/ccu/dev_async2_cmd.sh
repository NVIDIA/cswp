#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test basic device related commands in CSWP

# Set and clear log file
LOG_FILE="logs/dev_async2_cmd.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
execute_and_log "ccu get_dev0_reg 1 0x4e560000"
execute_and_log "ccu set_dev0_reg 1 0x4e560000 0x10001"
execute_and_log "ccu get_dev0_reg 1 0x4e560000"

# Test blocking async message reception
execute_and_log "ccu rd_async_msg"

# Needed since it doesn't reset when server is turned off.
execute_and_log "ccu set_dev0_reg 1 0x4e560000 0x0"
execute_and_log "ccu get_dev0_reg 1 0x4e560000"
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
