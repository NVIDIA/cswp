/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>

#include "ccu_out.h"

using namespace std;

/*
 * -------------------------------
 * process_cswp_init_out
 * -------------------------------
 */ 
void
process_init_cswp_out(uint64_t serverProtoV, const char* serverID, uint32_t serverV) {
    cout << "[ccu_out] CSWP server has started. Server Protocol Version is " << hex << showbase << serverProtoV << ", Server ID is " << serverID << ", Server Version is " << serverV <<"."<< endl;
}

/*
 * -------------------------------
 * process_cswp_term_out
 * -------------------------------
 */ 
void
process_term_cswp_out(void) {
    cout << "[ccu_out] CSWP server has stopped." << endl;
}

/*
 * -------------------------------
 * process_dev_open_out
 * -------------------------------
 */ 
void
process_dev_open_out(char** deviceInfoList, uint32_t deviceCount) {
    for (size_t i = 0; i < deviceCount; ++i) {
        cout << "[ccu_out] Device " << dec << noshowbase << i << ": " << deviceInfoList[i] << " (Status: OPEN)" << endl;
    }
}

/*
 * -------------------------------
 * process_dev_close_out
 * -------------------------------
 */ 
void
process_dev_close_out(void) {
    cout << "[ccu_out] All devices are closed." << endl;
}

/*
 * -------------------------------
 * process_get_dev_info_out
 * -------------------------------
 */ 
void
process_get_dev_info_out( char** deviceList, char** deviceTypes, uint32_t deviceCount) {
    for (size_t i = 0; i < deviceCount; ++i) { 
        cout << "[ccu_out] Device:" << i << ", Type:" << deviceTypes[i] << ", " << "Name:" << deviceList[i] << endl;
    }
}

/*
 * -------------------------------
 * process_get_sys_info_out
 * -------------------------------
 */ 
void
process_get_sys_info_out(unsigned descriptionFormat, unsigned descriptionSize, const uint8_t* descriptionDataBuffer) {

    if (descriptionSize == 0) {
        cerr << "[ccu_out] Received empty description. Please check if the server implements the CSWP_GET_SYSTEM_DESCRIPTION command or" << endl;
        cerr << "[ccu_out] that the file vera_sil.sdf exists for client to read." << endl;
        return;
    } else {
        cout << "[ccu_out] Description Size: " << descriptionSize << endl;
    }

    string filename;
    if (descriptionFormat == 0) {
        cout << "[ccu_out] Description Format: SDF" << endl;
        filename = "system_description.sdf";
    } else if (descriptionFormat == 1) {
        cout << "[ccu_out] Description Format: SDF gzipped" << endl;
        filename = "system_description.sdf.gz";
    } else {
        cerr << "[ccu_out] Description Format: UNKNOWN" << endl;
    }

    cout << "[ccu_out] Dumping data to "<< filename << "..." << endl;
    ofstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "[ccu_out] Could not open " << filename << "." << endl;
    } else {
        file.write(reinterpret_cast<const char*>(descriptionDataBuffer), descriptionSize);
        cout << "[ccu_out] " << filename << " is ready." << endl;
        if (file.fail()) {
            cerr << "[ccu_out] Failed to write to" << filename << "." << endl;
        }
        file.close();
    }
}

/*
 * -------------------------------
 * process_get_dev_cap_out
 * -------------------------------
 */ 
void
process_get_dev_cap_out(uint32_t capabilities, uint32_t capabilityData) {
    if ((capabilities & CSWP_CAP_REG)      == CSWP_CAP_REG)      { cout << "[ccu_out] CSWP_CAP_REG (MaxNumRegs:" << capabilityData << ")"<< endl; }
    if ((capabilities & CSWP_CAP_MEM)      == CSWP_CAP_MEM)      { cout << "[ccu_out] CSWP_CAP_MEM" << endl; }
    if ((capabilities & CSWP_CAP_MEM_POLL) == CSWP_CAP_MEM_POLL) { cout << "[ccu_out] CSWP_CAP_MEM_POLL" << endl; }
}

/*
 * -------------------------------
 * process_get_reg_list
 * -------------------------------
 */ 
void
process_get_reg_list_out(uint32_t registerCount, cswp_register_info_t * registerInfo) {
    cout << "[ccu_out] Name : Id : Size encoding : Display name : Description" << endl;
    for (uint32_t i = 0; i < registerCount; ++i) {
        cout << "[ccu_out] ";
        cout << registerInfo[i].name << " : ";
        cout << showbase << hex << registerInfo[i].id << " : ";
        cout << registerInfo[i].size << " : ";
        cout << registerInfo[i].displayName << " : ";
        cout << registerInfo[i].description << endl; 
    }
}

/*
 * -------------------------------
 * process_get_dev_reg_out
 * -------------------------------
 */ 
void
process_get_dev_reg_out(uint32_t* registerValue, size_t registerValueSize) {
    if (registerValueSize == 1) {
        cout << "[ccu_out] 0x" << hex << setfill('0') << setw(8)  << registerValue[0] << endl;
    } else if (registerValueSize == 2) {
        uint64_t wide_val = (uint64_t)registerValue[1];
        cout << "[ccu_out] 0x" << hex << setfill('0') << setw(16) << (registerValue[0] | ( wide_val << 32)) << endl;
    } else {
        cout << "[ccu_out] RegisterValue Size is not as expected. Allowed size are 32b or 64b." <<endl;
    }
}

/*
 * -------------------------------
 * process_set_dev_reg_out
 * -------------------------------
 */ 
void
process_set_dev_reg_out(void) {
    cout << "[ccu_out] Write has been sent but it can be ignored if it is not valid.\n          Please read-read to verify." << endl;
}

/*
 * -------------------------------
 * process_get_dev_mem_out
 * -------------------------------
 */ 
void
process_get_dev_mem_out(uint64_t address, uint8_t * buf, size_t bytesRead) {
    uint64_t val_64   = 0;
    uint32_t byte_pos = 0;
    uint64_t num_64b  = 0;
    for (uint32_t i = 0; i < bytesRead; ++i) {
        val_64 = val_64 | (((uint64_t)buf[i]) << (byte_pos*8));
        if (i % 8 == 0) { cout << endl << "[ccu_out] 0x" << hex << setfill('0') << setw(16) << (address+(num_64b*8)) << ": 0x"; num_64b++;}
        if (byte_pos == 7) {
            cout << hex << setfill('0') << setw(16) << val_64; 
            byte_pos = 0; val_64 = 0;
        } else if ((i+1) == bytesRead) {
            uint32_t print_w = ((byte_pos+1)*2);
            cout << hex << setfill('0') << setw(print_w) << val_64; 
        } else {
            byte_pos++;
        }
    }
    cout << endl;
}

/*
 * -------------------------------
 * process_dump_dev_mem_out
 * -------------------------------
 */ 
void
process_dump_dev_mem_out(uint8_t * buf, size_t bytesRead) {

    ofstream file("mem_dump.bin", ios::binary);

    if (!file.is_open()) {
        cerr << "[ccu_out] Could not open mem_dump.bin to dump read data." << endl;
    } else {
        cout << "[ccu_out] Dumping data to mem_dump.bin..." << endl;
        file.write(reinterpret_cast<const char*>(buf), bytesRead);
        cout << "[ccu_out] mem_dump.bin is ready." << endl;
        if (file.fail()) {
            cerr << "[ccu_out] Failed to write to mem_dump.bin" << endl;
        }
        file.close();
    }
}

/*
 * -------------------------------
 * process_set_dev_mem_out
 * -------------------------------
 */ 
void
process_set_dev_mem_out(void) {
    cout << "[ccu_out] Write has been sent but it can be ignored if it is not valid.\n          Please read-read to verify." << endl;
}

/*
 * -------------------------------
 * process_async_message_out
 * -------------------------------
 */ 
void
process_async_message_out(uint64_t deviceNo, uint64_t msg_level, char * msg) {
    string level = "";

    if (msg_level == 0) {
        level = "error";
    } else if (msg_level == 1) {
        level = "warning";
    } else if (msg_level == 2) {
        level = "info";
    } else if (msg_level == 3) {
        level = "debug";
    } else {
        level = "UNKNOWN MSG LEVEL";
    }
    cout << "[ccu_out] Received " << level << " async msg from device " << deviceNo << " -"<< endl;
    cout << "*********" << endl << msg << endl << "*********" << endl;
}
