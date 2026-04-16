#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Function to execute command and log output
execute_and_log() {
    local cmd="$1"
    echo "$ $cmd" | tee -a "$LOG_FILE"
    $cmd > >(tee -a "$LOG_FILE") 2>&1
    echo "-------------------------" | tee -a "$LOG_FILE"
}

# Function to rename file if exists.
check_and_rename_file() {
    local file_path="$1"
    local new_name="$2"
    if [ -f "$file_path" ]; then
        echo "Renaming $file_path to $new_name and moved to dump_bins dir." | tee -a "$LOG_FILE"
        execute_and_log "mv $file_path dump_bins/$new_name"
    fi
}

