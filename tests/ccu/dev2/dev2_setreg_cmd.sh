#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test for device 2 register setreg commands

# Set and clear log file
LOG_FILE="logs/dev2/dev2_setreg.log"
> "$LOG_FILE"

source tests/utility.sh


# START server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev2_reg 2 0x180e18"
execute_and_log "ccu set_dev2_reg 2 0x180e18 0x2222222244444444"
execute_and_log "ccu get_dev2_reg 2 0x180e18"

execute_and_log "ccu get_dev2_reg 1 0x180e18"
execute_and_log "ccu set_dev2_reg 1 0x180e18 0x11111111"
execute_and_log "ccu get_dev2_reg 2 0x180e18"
#-----------------------------------------------------------------------
# must fail in ccu or will be ignored
execute_and_log "ccu set_dev2_reg 2 0x30000000 0x10001" # Out of bounds
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
