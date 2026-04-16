#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Test basic device related commands in CSWP

# Set and clear log file
LOG_FILE="logs/dev_basic_cmds.log"
> "$LOG_FILE"

source tests/utility.sh

# STRAT server
execute_and_log "ccu init_cswp"
#-----------------------------------------------------------------------
# must pass
execute_and_log "ccu help"
execute_and_log "ccu get_sys_info"
execute_and_log "ccu get_dev_info"
execute_and_log "ccu get_dev0_cap"
execute_and_log "ccu get_dev1_cap"
execute_and_log "ccu get_dev2_cap"
execute_and_log "ccu get_dev0_reg_list"
#-----------------------------------------------------------------------
# STOP server
execute_and_log "ccu term_cswp"
echo -e "Test completed!";
