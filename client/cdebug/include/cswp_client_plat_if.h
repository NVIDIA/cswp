/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSWP_CLIENT_PLAT_IF_H
#define CSWP_CLIENT_PLAT_IF_H

#include "transport_cfg.h"
#include "cswp_client.h"
#include "usb_device.h"
#include "plat_sync_prim.h"

#include <stdexcept>
#include <cstring>
#include <stdbool.h>
#include <queue>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

//--------------------------------------------------------------------
// Struct usbdata
//--------------------------------------------------------------------
typedef struct {
    uint8_t data[CSWP_REQ_RSP_BUFFER_SIZE];
    size_t  size = CSWP_REQ_RSP_BUFFER_SIZE;
    size_t  used;
} usbdata;

//--------------------------------------------------------------------
// Class CSWPBase
//--------------------------------------------------------------------
class CSWPBase {
    public:
        CSWPBase(transport_cfg_t transport_cfg);
        ~CSWPBase();
    
        // Transport functions
        int  connect();
        void disconnect();
        int  send(const void* data, size_t size);
        int  receive(void* data, size_t size, size_t* used);
        int  receive_async(void* data, size_t size, size_t* used);
    
        // CSWP wrapper functions
        int cswp_init(const char* clientID, uint64_t ProtocolVersion, uint64_t* serverProtocolVersion, char* serverID, size_t serverIDSize, unsigned* serverVersion);
        int cswp_term();
        int cswp_batch_begin(int abortOnError);
        int cswp_batch_end(unsigned* opsCompleted);
        int cswp_client_info(const char* message);
        int cswp_set_devices(unsigned deviceCount, const char** deviceList, const char** deviceTypes);
        int cswp_get_devices(unsigned* deviceCount, char** deviceList, size_t deviceListSize, size_t deviceListEntrySize, char** deviceTypes, size_t deviceTypeSize,
                             size_t deviceTypeEntrySize);
        int cswp_get_system_description(unsigned* descriptionFormat, unsigned* descriptionSize, uint8_t* descriptionDataBuffer, size_t bufferSize);
        int cswp_device_open(unsigned deviceNo, char* deviceInfo, size_t deviceInfoSize);
        int cswp_device_close(unsigned deviceNo);
        int cswp_set_config(varint_t deviceNo, const char* name, const char* value);
        int cswp_get_config(varint_t deviceNo, const char* name, char* value, size_t valueSize);
        int cswp_get_device_capabilities(varint_t deviceNo, unsigned* capabilities, unsigned* capabilityData);
        int cswp_device_reg_list(unsigned deviceNo, unsigned* registerCount, cswp_register_info_t* registerInfo, size_t registerInfoSize, char *strBuf, size_t strBufSize);
        int cswp_device_reg_read(unsigned deviceNo, size_t registerCount, const uint64_t* registerIDs, uint32_t* registerValues, size_t registerValuesSize);
        int cswp_device_reg_write(unsigned deviceNo, size_t registerCount, const uint64_t* registerIDs, const uint32_t* registerValues, size_t registerValuesSize);
        int cswp_device_mem_read(unsigned deviceNo, uint64_t address, size_t size, cswp_access_size_t accessSize, unsigned flags, uint8_t* buf, size_t* bytesRead);
        int cswp_device_mem_write(unsigned deviceNo, uint64_t address, size_t size, cswp_access_size_t accessSize, unsigned flags, const uint8_t* buf);
        int cswp_device_mem_poll(unsigned deviceNo, uint64_t address, size_t size, cswp_access_size_t accessSize, unsigned flags, unsigned tries, unsigned interval,
                                 const uint8_t* mask, const uint8_t* value, uint8_t* buf, size_t* bytesRead);
        int cswp_async_message(varint_t* deviceNo, varint_t* msg_level, char* msg, size_t msg_size);
        const char *cswp_decode_error(int e);
        std::string get_client_error(void);
    private:
        cswp_client_t              m_cswpClient;
        std::string                m_transport; // Type of transport
        // For TCP/socket transport
        struct addrinfo*           m_server_info;
        int                        m_socket_fd;
        // For USB transport
        std::string                m_serialNumber;
        int                        m_vendor_id;
        int                        m_product_id;
        int                        m_speed_id;
        int                        m_interface_id;
        std::unique_ptr<USBDevice> m_usb;
        int                        m_epCmd;
        int                        m_epRsp;
        // Reading thread for responses
        std::thread                readThread;
        PlatMutex                  readThread_lock;
        bool                       readThread_running = false;
        // Raw response data queue
        PlatMutex                  respQ_lock;
        PlatThreadCondVar          respQCond;
        std::queue<usbdata>        respQ;
        // Raw async data queue
        PlatMutex                  asyncQ_lock;
        std::queue<usbdata>        asyncQ;
    
        int  listen(void);         // Function for readThread. It monitors incoming responses and sorts them resp queues
        void init_transport_info(transport_cfg_t transport);
        void init_client_info(void);
};

CSWPBase* CreateCSWP(transport_cfg_t transport_cfg);

#endif // CSWP_CLIENT_PLAT_IF_H
