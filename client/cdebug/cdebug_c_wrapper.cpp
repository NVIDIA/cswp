/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

// This wrapper is needed to use CSWP API in C-Only projects like OpenOCD.
// The goal is to open up CSWP debug API inside CSWPBase class.

#include "cswp_client_plat_if.h"

extern "C" {

// Opaque handle for C code
typedef void* cswp_handle_t;

// Create a new CSWPBase instance
cswp_handle_t
cswp_create_c(const char* type, const char* cfg_str) {
    transport_cfg_t transport_cfg;
    transport_cfg.type = type;
    // For USB transport cfg_str is in the format of "vid:pid:sid:iid"
    // For TCP transport cfg_str is in the format of "ipaddr:portid"
    int result = 0;
    char serial[200]; // To account for usb standard
    char ipaddr[50];  // To account for ipv6
    if (transport_cfg.type == "usb") {
        // Defaults
        transport_cfg.serial = "";
        transport_cfg.iid = 0;
        transport_cfg.sid = 0;
        result = sscanf(cfg_str, "%x:%x:%199[^:]:%d:%d", &transport_cfg.vid, &transport_cfg.pid, serial, &transport_cfg.sid, &transport_cfg.iid);
        if(result < 2) {
            return NULL;
        } else if(result == 2) {
            // Check if the user attempted to set speed and interface without serial
            sscanf(cfg_str, "%x:%x::%d:%d", &transport_cfg.vid, &transport_cfg.pid, &transport_cfg.sid, &transport_cfg.iid);
        } else if(result >= 3) {
            transport_cfg.serial = serial;
        }
    } else if (transport_cfg.type == "tcp") { 
        result = sscanf(cfg_str, "%49[^:]:%d", ipaddr, &transport_cfg.portid);
        if(result != 2) {
            return NULL;
        }
        transport_cfg.ipaddr = ipaddr;
    }
    return CreateCSWP(transport_cfg);
}

// Destroy the CSWPBase instance
void
cswp_delete_c(cswp_handle_t handle) {
    delete static_cast<CSWPBase*>(handle);
}

int
cswp_init_c(cswp_handle_t handle, const char* clientID, uint64_t ProtocolVersion,
            uint64_t* serverProtocolVersion, char* serverID, size_t serverIDSize, unsigned* serverVersion) {
    return static_cast<CSWPBase*>(handle)->cswp_init(clientID, ProtocolVersion, serverProtocolVersion,
                                                     serverID, serverIDSize, serverVersion);
}

int
cswp_term_c(cswp_handle_t handle) {
    return static_cast<CSWPBase*>(handle)->cswp_term();
}

int
cswp_device_open_c(cswp_handle_t handle, unsigned deviceNo, char* deviceInfo, size_t deviceInfoSize) {
    return static_cast<CSWPBase*>(handle)->cswp_device_open(deviceNo, deviceInfo, deviceInfoSize);
}

int
cswp_device_close_c(cswp_handle_t handle, unsigned deviceNo) {
    return static_cast<CSWPBase*>(handle)->cswp_device_close(deviceNo);
}

int
cswp_get_devices_c(cswp_handle_t handle, unsigned* deviceCount, char** deviceList, size_t deviceListSize,
                   size_t deviceListEntrySize, char** deviceTypes, size_t deviceTypeSize,
                   size_t deviceTypeEntrySize) {
    return static_cast<CSWPBase*>(handle)->cswp_get_devices(deviceCount, deviceList, deviceListSize,
                                                            deviceListEntrySize, deviceTypes, deviceTypeSize,
                                                            deviceTypeEntrySize);
}

int
cswp_get_device_capabilities_c(cswp_handle_t handle, varint_t deviceNo, unsigned* capabilities,
                               unsigned* capabilityData) {
    return static_cast<CSWPBase*>(handle)->cswp_get_device_capabilities(deviceNo, capabilities, capabilityData);
}

int 
cswp_device_reg_list_c(cswp_handle_t handle, unsigned deviceNo, unsigned* registerCount,
                       cswp_register_info_t* registerInfo, size_t registerInfoSize,
                       char *strBuf, size_t strBufSize) {
    return static_cast<CSWPBase*>(handle)->cswp_device_reg_list(deviceNo, registerCount, registerInfo,
                                                                registerInfoSize, strBuf, strBufSize);
}

int
cswp_device_reg_read_c(cswp_handle_t handle, unsigned deviceNo, size_t registerCount, const uint64_t* registerIDs,
                       uint32_t* registerValues, size_t registerValuesSize) {
    return static_cast<CSWPBase*>(handle)->cswp_device_reg_read(deviceNo, registerCount, registerIDs,
                                                                registerValues, registerValuesSize);
}

int 
cswp_device_reg_write_c(cswp_handle_t handle, unsigned deviceNo, size_t registerCount, const uint64_t* registerIDs,
                        const uint32_t* registerValues, size_t registerValuesSize) {
    return static_cast<CSWPBase*>(handle)->cswp_device_reg_write(deviceNo, registerCount, registerIDs,
                                                                 registerValues, registerValuesSize);
}

int
cswp_device_mem_read_c(cswp_handle_t handle, unsigned deviceNo, uint64_t address, size_t size,
                       cswp_access_size_t accessSize, unsigned flags, uint8_t* buf, size_t* bytesRead) {
    return static_cast<CSWPBase*>(handle)->cswp_device_mem_read(deviceNo, address, size, accessSize, flags, buf, bytesRead);
}

int
cswp_device_mem_write_c(cswp_handle_t handle, unsigned deviceNo, uint64_t address, size_t size,
                        cswp_access_size_t accessSize, unsigned flags, const uint8_t* buf) {
    return static_cast<CSWPBase*>(handle)->cswp_device_mem_write(deviceNo, address, size, accessSize, flags, buf);
}

int
cswp_device_mem_poll_c(cswp_handle_t handle, unsigned deviceNo, uint64_t address, size_t size,
                       cswp_access_size_t accessSize, unsigned flags, unsigned tries, unsigned interval,
                       const uint8_t* mask, const uint8_t* value, uint8_t* buf, size_t* bytesRead) {
    return static_cast<CSWPBase*>(handle)->cswp_device_mem_poll(deviceNo, address, size, accessSize, flags, tries,
                                                                interval, mask, value, buf, bytesRead);
}

int
cswp_async_message_c(cswp_handle_t handle, varint_t* deviceNo, varint_t* msg_level, char* msg, size_t msg_size) {
    return static_cast<CSWPBase*>(handle)->cswp_async_message(deviceNo, msg_level, msg, msg_size);
}

int
cswp_batch_begin_c(cswp_handle_t handle, int abortOnError) {
    return static_cast<CSWPBase*>(handle)->cswp_batch_begin(abortOnError);
}

int 
cswp_batch_end_c(cswp_handle_t handle, unsigned* opsCompleted) {
    return static_cast<CSWPBase*>(handle)->cswp_batch_end(opsCompleted);
}

const char *cswp_decode_error_c(cswp_handle_t handle, int e) {
    return static_cast<CSWPBase*>(handle)->cswp_decode_error(e);
}

}
