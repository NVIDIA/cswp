#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVidia Corporation. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# See LICENSE.txt for the full license.

# Sanity test for CSWP over TCP/Socket using ccu_dummy_server

# Define your test_cases in an array
test_cases=(
"init_cswp"
"term_cswp"
"get_dev_info"
"get_sys_info"
"get_dev0_cap"
"get_dev1_cap"
"get_dev2_cap"
"get_dev0_reg_list"
"get_dev0_reg 1 0xd00"
"get_dev0_reg 1 0xdf8"
"get_dev0_reg 1 0xdfc"
"get_dev0_reg 1 0x4e560000"
"set_dev0_reg 1 0x4e560000 0x10001"
"get_dev2_reg 1 0x2300"
"set_dev2_reg 1 0x2300 0x4554"
"get_dev0_mem  0x80000000 124"
"dump_dev0_mem 0x80000000 124"
"set_dev0_mem  0x80000000 4 0x44"
"fill_dev0_mem 0x80000000 mem_dump.bin"
"get_dev1_mem  0x80000000 8"
"dump_dev1_mem 0x80000000 16"
"set_dev1_mem  0x80000000 8 0x44"
"fill_dev1_mem 0x80000000 mem_dump.bin")

echo "---------------------------------------"
for cmd in "${test_cases[@]}"; do
    echo "+ ccu $cmd"

    # Start the server in the background
    ./ccu_dummy_server &
    server_pid=$!

    # Wait for the server to be ready
    sleep 1

    # Send the command to the client
    read -ra args <<< "$cmd"
    ./ccu "${args[@]}"

    # Wait for the client to be disconnected
    sleep 1

    # Wait for server to exit or kill it if alive
    if kill -0 "$server_pid" 2>/dev/null; then
        kill "$server_pid"
        wait "$server_pid" 2>/dev/null  # clean up
    fi
    echo "---------------------------------------"
done
echo "Test complete!"
