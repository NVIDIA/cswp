/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

// This file contains functions that extract command arguments from command
// line string and create actual arguments for CSWP functions.

#include <iostream>
#include <fstream>
#include <iterator>
#include <climits>
#include <vector>
#include <cstring>

#include "ccu_in.h"

using namespace std;

#define PRINT_EXTRACT_ERR(msg)                \
    do {                                      \
        cout << "[  ccu_in] " << msg << endl; \
    } while (0)

/*
 * -------------------------------
 * Utility functions
 * -------------------------------
*/
// is_valid_dec_str ----------------------------------------------------------
static inline bool
is_valid_dec_str(const char* str) {

    if (str == NULL) {
        PRINT_EXTRACT_ERR("Decimal string cannot be NULL.");
        return false; 
    }

    for (size_t i = 0; i < strlen(str); i++) {
        if ((str[i] < '0') || (str[i] > '9')) {
            PRINT_EXTRACT_ERR("Decimal string contains non-decimal character.");
            return false;
        }
    }
    return true;
}
// is_valid_hex_str ----------------------------------------------------------
static inline bool
is_valid_hex_str(const char* hexstr, uint64_t* value) {

    if ((hexstr == NULL)   ||
        strlen(hexstr) < 3 ||
        (hexstr[0] != '0'  ||
        (hexstr[1] != 'x' && hexstr[1] != 'X'))) {
        PRINT_EXTRACT_ERR("Hex string must be in given in hex format and be prefixed with 0x/0X.");
        return false;
    }

    unsigned long long llvalue; // For all platform compatibility
    if (sscanf(hexstr, "%llx", &llvalue) != 1) {
        PRINT_EXTRACT_ERR("Hex string contains non-hex character.");
        return false;
    }

    *value = (uint64_t)llvalue;

    return true;
}
// is_valid_devID ----------------------------------------------------------
static inline bool
is_valid_devID(const char devchar, uint32_t* devID) {

    if (!(devchar >= '0' && devchar <= '2')) {
        PRINT_EXTRACT_ERR("Invalid device ID. Allowed values are 0, 1 and 2.");
        return false;
    }

    *devID = (uint32_t)(devchar - '0'); 
    return true;
}
// is_valid_regW ----------------------------------------------------------
static inline bool
is_valid_regW(const char* szstr, uint32_t devID, size_t* size) {
    if      (strcmp(szstr, "1") == 0) { *size = 1; return true; }
    else if (strcmp(szstr, "2") == 0) {
        if (devID == 0) {
            PRINT_EXTRACT_ERR("Device 0 only supports 32b reg sizes.");
            return false; 
        } else {
            *size = 2; 
            return true;
        }
    } else {
        PRINT_EXTRACT_ERR("Register size can only be 1(32b) or 2(64b).");
        return false;
    }
}
// is_valid_get_mem_rd_sz ----------------------------------------------------------
static inline bool
is_valid_get_mem_rd_sz(const char* szstr, uint64_t* size, bool op_type_dump) {

    if (!is_valid_dec_str(szstr)) {
        PRINT_EXTRACT_ERR("Read size needs to be given in decimal format and cannot be NULL.");
    }

    unsigned long long llvalue; // For all platform compatibility
    if (sscanf(szstr, "%llu", &llvalue) != 1) {
        PRINT_EXTRACT_ERR("Read size is invalid.");
        return false;
    }

    if ((!op_type_dump) && (llvalue > 400)) {
        PRINT_EXTRACT_ERR("For sizes > 400 bytes, use dump_dev*_mem command.");
        return false;
    }

    *size = (uint64_t)llvalue;
    return true;
}
// is_valid_set_mem_wr_sz ----------------------------------------------------------
static inline bool
is_valid_set_mem_wr_sz(const char* szstr, uint64_t* size, uint32_t devID) {

    if      (strcmp(szstr, "1") == 0) { *size = 1; }
    else if (strcmp(szstr, "2") == 0) { *size = 2; }
    else if (strcmp(szstr, "4") == 0) { *size = 4; }
    else if (strcmp(szstr, "8") == 0) { *size = 8; }
    else {
        PRINT_EXTRACT_ERR("Write size for set_dev*_mem can only be 1(8b), 2(16b), 4(32b) or 8(64b).\n           Also, for size > 8 byte writes, use fill_dev*_mem command.");
        return false;
    }

    if ((devID == 0) && (*size != 4)) {
        PRINT_EXTRACT_ERR("Minimum write size for set_dev*_mem is 4(32b) for device 0.\n           Also, for size > 4 byte writes, use fill_dev*_mem command.");
        return false; 
    }

    return true;
}
// is_mem_start_addr_aligned ----------------------------------------------------------
static inline bool
is_mem_start_addr_aligned(uint32_t devID, uint64_t addr, uint64_t total_bytes, bool op_type_fill) {

    if (devID == 0) {
        if ((addr & 0x3) != 0x0) {
            PRINT_EXTRACT_ERR("Memory op start_addr needs to be 32b aligned for device 0.");
            return false;
        }
    }

    if (devID == 1) {
        if (op_type_fill) {
            if ((addr & 0x7) != 0x0) {
                PRINT_EXTRACT_ERR("Fill memory op's start_addr needs to be 64b aligned for device 1.");
                return false;
            }
        } else {
            if (((total_bytes == 2) && ((addr & 0x1) != 0x0)) ||
                (((total_bytes == 3) || 
                  (total_bytes == 4)) && ((addr & 0x3) != 0x0)) ||
                ((total_bytes > 4) && ((addr & 0x7) != 0x0))) {
                PRINT_EXTRACT_ERR("Memory op's start_addr needs to be aligned to 8b/16b/32b/64b for a \n           given size for device 1. Check CCU spec for more details.");
                return false;
            }
        }
    }
    return true;
}
// is_fill_data_ok ----------------------------------------------------------
static inline bool
is_fill_data_ok(char* filename, mem_cmd_t* m) {

    ifstream file(filename, ios::binary);

    if (!file.good()) {
        PRINT_EXTRACT_ERR("File with fill data cannot be found or opened.");
        return false;
    }

    // Read in the bytes from file into a vector
    vector<uint8_t> buffer((istreambuf_iterator<char>(file)),
                            istreambuf_iterator<char>());
    size_t file_sz = buffer.size();

    // Size checks
    if (m->devID == 0) {
        if (file_sz > DEV0_MEM_MAX_BIN_SZ) {
            PRINT_EXTRACT_ERR("File size cannot be larger than 4kB.");
            return false;
        }
        if (file_sz <= 0x4) {
            PRINT_EXTRACT_ERR("File size needs to be > 4 bytes.\n          For < 4 byte writes, use set_dev*_mem command.");
            return false; 
        }
        if ((file_sz % 0x4) != 0) {
            PRINT_EXTRACT_ERR("File size needs to be multiple of 4 bytes for device 0.");
            return false; 
        }
    } else {
        if (file_sz > DEV1_MEM_MAX_BIN_SZ) {
            PRINT_EXTRACT_ERR("File size cannot be larger than 256kB.");
            return false;
        }
        if (file_sz <= 0x8) {
            PRINT_EXTRACT_ERR("File size needs to be > 8 bytes.\n          For < 8 byte writes, use set_dev*_mem command.");
            return false;
        }
    }

    // Push vector data into a byte array
    for (size_t i = 0; i < file_sz; i++) { m->rw_values[i] = buffer[i]; }
    m->total_bytes = file_sz;

    return true;
}
// validate_and_get_etrID ----------------------------------------------------------
static inline bool
validate_and_get_etrID (char* etrStr, int* etrID) {

    if (sscanf(etrStr, "%d", etrID) != 1) {
        PRINT_EXTRACT_ERR("Invalid ETRID."); 
        return false;
    }

    if (((*etrID) > (int)MAX_NUM_ETRS) || ((*etrID) < 0)) {
        PRINT_EXTRACT_ERR("ETRID is not found."); 
        return false;
    }
    
    return true;
}

/*
 * -------------------------------
 * extract_get_dev_cap_param
 * -------------------------------
 */ 
bool
extract_get_dev_cap_param(int argc, char* argv[], dev_cmd_t* d) {

    if (argc != 2) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments.");
        return false;
    }

    char devchar = argv[1][7]; // Position of devchar is fixed

    uint32_t devID = 0;
    if (!is_valid_devID(devchar, &devID)) {
        return false;
    }
    d->devID = devID;

    return true;
}

/*
 * -------------------------------
 * extract_get_dev_reg_param
 * -------------------------------
 */ 
bool
extract_get_dev_reg_param(int argc, char* argv[], reg_cmd_t* r) {

    if (argc != 4) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments.");
        return false; 
    }

    uint64_t regID;
    if (!(is_valid_devID(argv[1][CCU_3LCMD_DEVCHAR_POS], &r->devID) &&
          is_valid_hex_str(argv[3], &regID))) {
        return false;
    }
    r->regID = regID;
    r->value = 0; // N/A in this case. Set to default.

    // Separate check as the devID need to be validated before.
    if (!is_valid_regW(argv[2], r->devID, &r->regSZ)) { return false; };

    return true;
}

/*
 * -------------------------------
 * extract_set_dev_reg_param
 * -------------------------------
 */
bool
extract_set_dev_reg_param(int argc, char* argv[], reg_cmd_t* r) {

    if (argc != 5) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments.");
        return false; 
    }

    uint64_t regID;
    if (!(is_valid_devID(argv[1][CCU_3LCMD_DEVCHAR_POS], &r->devID) &&
          is_valid_hex_str(argv[3], &regID)  &&
          is_valid_hex_str(argv[4], &r->value))) {
        return false; 
    }
    r->regID = (uint32_t)regID;

    // Separate check as the devID need to be validated before.
    if (!is_valid_regW(argv[2], r->devID, &r->regSZ)) { return false; };

    return true;
}

/*
 * -------------------------------
 * extract_get_dev_mem_param
 * -------------------------------
 */
bool
extract_get_dev_mem_param(int argc, char* argv[], mem_cmd_t* m) {

    if (argc != 4) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments.");
        return false;
    }

    if (!(is_valid_devID(argv[1][CCU_3LCMD_DEVCHAR_POS], &m->devID) &&
          is_valid_hex_str(argv[2], &m->start_addr) &&
          is_valid_get_mem_rd_sz(argv[3], &m->total_bytes, false /*op_type_dump*/))) {
        return false;
    }

    // Separate check as the devID, start_addr and total_bytes need to be validated before.
    if (!is_mem_start_addr_aligned(m->devID, m->start_addr, m->total_bytes, false/*op_type_fill*/)) { return false; };

    // Do an extra read for dev0 if size is not multiple of 4
    if ((m->devID == 0) && ((m->total_bytes % 4) != 0)) {
        m->total_bytes = m->total_bytes + (4 - (m->total_bytes % 4));
    }

    m->flags = 0; // N/A set to default 0

    return true;
}

/*
 * -------------------------------
 * extract_dump_dev_mem_param
 * -------------------------------
 */
bool
extract_dump_dev_mem_param(int argc, char* argv[], mem_cmd_t* m) {

    if (argc != 4) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments.");
        return false;
    }

    if (!(is_valid_devID(argv[1][CCU_4LCMD_DEVCHAR_POS], &m->devID) &&
          is_valid_hex_str(argv[2], &m->start_addr) &&
          is_valid_get_mem_rd_sz(argv[3], &m->total_bytes, true /*op_type_dump*/))) {
        return false;
    }

    // Separate check as the devID, start_addr and total_bytes need to be validated before.
    if (!is_mem_start_addr_aligned(m->devID, m->start_addr, m->total_bytes, false/*op_type_fill*/)) { return false; };

    if (m->devID == 0) {
        if (m->total_bytes > DEV0_MEM_MAX_BIN_SZ) {
            PRINT_EXTRACT_ERR("File size cannot be larger than 4kB.");
            return false;
        } 
    } else {
        if (m->total_bytes > DEV1_MEM_MAX_BIN_SZ) {
            PRINT_EXTRACT_ERR("File size cannot be larger than 256kB.");
            return false;
        } 
    }

    // Do an extra read for dev0 if size is not multiple of 4
    if ((m->devID == 0) && ((m->total_bytes % 4) != 0)) {
        m->total_bytes = m->total_bytes + (4 - (m->total_bytes % 4));
    }

    m->flags = 0; // N/A set to default 0

    return true;
}

/*
 * -------------------------------
 * extract_set_dev_mem_param
 * -------------------------------
 */
bool
extract_set_dev_mem_param(int argc, char* argv[], mem_cmd_t* m) {

    if (argc != 5) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments.");
        return false;
    }

    uint64_t value;
    if (!(is_valid_devID(argv[1][CCU_3LCMD_DEVCHAR_POS], &m->devID) &&
          is_valid_hex_str(argv[2], &m->start_addr) &&
          is_valid_hex_str(argv[4], &value))) {
        return false;
    }

    // Separate checks as the devID and size needs to be validated before.
    if (!(is_valid_set_mem_wr_sz(argv[3], &m->total_bytes, m->devID)  &&
           is_mem_start_addr_aligned(m->devID, m->start_addr, m->total_bytes, false/*op_type_fill*/))) { return false; };

    // Convert value into a byte array
    for (uint64_t i=0; i < m->total_bytes; i++ ) {
        m->rw_values[i] = (uint8_t)((value >> (i*8)) & 0xFF);
    }

    m->flags = 0; // N/A set to default 0
    
    return true;
}

/*
 * -------------------------------
 * extract_fill_dev_mem_param
 * -------------------------------
 */
bool
extract_fill_dev_mem_param(int argc, char* argv[], mem_cmd_t* m) {

    if (argc != 4 ) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments."); 
        return false; 
    }

    if (!(is_valid_devID(argv[1][CCU_4LCMD_DEVCHAR_POS], &m->devID) &&
          is_valid_hex_str(argv[2], &m->start_addr))) {
        return false;
    }

    // Seperate checks as devID and start_addr need to be validated first
    if (!((is_mem_start_addr_aligned(m->devID, m->start_addr, m->total_bytes, true/* op_type_fill*/)) &&
          is_fill_data_ok(argv[3], m))) { return false; }

    m->flags = 0; // N/A set to default 0

    return true;
}

/*
 * -------------------------------
 * extract_perf_cstrace_param
 * -------------------------------
 */
bool
extract_perf_cstrace_param(int argc, char* argv[], cstrace_cmd_t* c) {

    if (argc != 3) {
        PRINT_EXTRACT_ERR("Unexpected number of arguments."); 
        return false;
    }
    return validate_and_get_etrID(argv[2], &c->etrID);
}