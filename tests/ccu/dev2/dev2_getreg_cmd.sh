#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test for device 2 register getreg commands

# Set and clear log file
LOG_FILE="logs/dev2/dev2_getreg.log"
> "$LOG_FILE"

source tests/utility.sh

# START server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu get_dev2_reg 1 0x0"
execute_and_log "ccu get_dev2_reg 2 0x0"
execute_and_log "ccu get_dev2_reg 2 0x280090"
#-----------------------------------------------------------------------
# must fail in ccu
execute_and_log "ccu get_dev2_reg 2 0x30000000" # out of bounds
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
