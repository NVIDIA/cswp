/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#include "cswp_client_plat_if.h"
#include "cswp_client_ex.h"
#include <system_error>

//--------------------------------------------------------------------
// CSWPBase
//
// Constructor and Destructor
//--------------------------------------------------------------------
CSWPBase::CSWPBase(transport_cfg_t transport_cfg) :
      m_epCmd(-1),
      m_epRsp(-1),
      m_socket_fd(-1)
{
    init_transport_info(transport_cfg);
    init_client_info();
}

CSWPBase::~CSWPBase() {
    cswp_client_term(&m_cswpClient);
    if (m_transport == "tcp") {
        // Free the linked list of results when done
        freeaddrinfo(m_server_info);
    }
}

//--------------------------------------------------------------------
// CSWPBase::connect
//--------------------------------------------------------------------
int
CSWPBase::connect() {

    if (m_transport == "usb") {
        USBDeviceIdentifier USBID(m_vendor_id, m_product_id, m_interface_id, m_speed_id);

        m_usb = USBDevice::create(&USBID, m_serialNumber);

        m_usb->connect();

        // identify endpoints
        std::vector<USBEPInfo> epInfo = m_usb->getEndPoints();
        for (std::vector<USBEPInfo>::const_iterator ep = epInfo.begin();
            ep != epInfo.end();
            ++ep) {
            if ((ep->addr & USBEPInfo::EP_DIR_MASK) == USBEPInfo::EP_DIR_OUT &&
                ep->type == USBEPInfo::EP_TYPE_BULK) {
                m_epCmd = ep->addr;
            } else if ((ep->addr & USBEPInfo::EP_DIR_MASK) == USBEPInfo::EP_DIR_IN &&
                    ep->type == USBEPInfo::EP_TYPE_BULK) {
                m_epRsp = ep->addr;
            }
        }

        if (m_epCmd == -1) {
            throw USBException("Failed to find command endpoint");
            return CSWP_COMMS;
        } else if (m_epRsp == -1) {
            throw USBException("Failed to find response endpoint");
            return CSWP_COMMS;
        } else {
            // Connection is successful, create a readThread.
            PlatMutex::ScopedLock lock(readThread_lock);
            readThread_running = true;
            try {
                readThread = std::thread(&CSWPBase::listen, this);
            }
            catch (const std::system_error& e) {
                readThread_running = false;
                return CSWP_COMMS;
            }
        }
    } else {
        // Create socket
        m_socket_fd = ::socket(m_server_info->ai_family, 
                             m_server_info->ai_socktype, 
                             m_server_info->ai_protocol);
        if (m_socket_fd < 0) {
            throw CDebugEx(CSWP_COMMS, "Error creating socket");
            return CSWP_COMMS;
        }

        // Connect to server
        if (::connect(m_socket_fd, m_server_info->ai_addr, m_server_info->ai_addrlen) < 0) {
            throw CDebugEx(CSWP_COMMS, "Error connecting to server");
            return CSWP_COMMS;
        }
    }
    return CSWP_SUCCESS;
}

//--------------------------------------------------------------------
// CSWPBase::disconnect
//--------------------------------------------------------------------
void
CSWPBase::disconnect() {

    if (m_transport == "usb") {
        PlatMutex::ScopedLock lock(readThread_lock);
        if (readThread_running) {
            readThread_running = false; // Stop read thread
            m_usb->cancelTransfers();
            // Wait for read thread to finish
            if (readThread.joinable()) {
                readThread.join();
            }
        }
        // Check in case disconnect is called without connect
        if (m_usb)
            m_usb->disconnect(); // Disconnect USB
    } else {
        if (m_socket_fd >= 0) {
            ::close(m_socket_fd);
            m_socket_fd = -1;
        }
    }
}

//--------------------------------------------------------------------
// CSWPBase::send
//--------------------------------------------------------------------
int
CSWPBase::send(const void* data, size_t size) {

    if (m_transport == "usb") {
        if (m_epCmd < 0)
            return CSWP_NOT_INITIALIZED;
        int cmdToken = m_usb->submitWriteTransfer(m_epCmd, data, size);
        while (1) {
            USBDevice::Transfer_Status status;
            size_t used;
            int token = m_usb->completeTransfer_forToken(cmdToken, &status, &used);
            if (token == cmdToken) {
                if (status != USBDevice::Transfer_SUCCESS || used < size) {
                    throw USBException("Failed to send command");
                    return CSWP_COMMS;
                }
                break;
            }
        }
    } else {
        if (m_socket_fd < 0)
            return CSWP_NOT_INITIALIZED;
        size_t bytes_sent = ::send(m_socket_fd, data, size, 0);
        if (bytes_sent != size) {
            throw CDebugEx(CSWP_COMMS, "Sent bytes are not same as requested.");
            return CSWP_COMMS;
        }
    }
    return CSWP_SUCCESS;
}

//--------------------------------------------------------------------
// CSWPBase::listen
//
// Keeps reading all incoming data on the USB port and sorts it into
// CSWP reponse and async data.
//--------------------------------------------------------------------
int
CSWPBase::listen(void) {

    try {
        while (readThread_running) {
            usbdata readData;
            int rspToken = m_usb->submitReadTransfer(m_epRsp, &readData.data, readData.size);
            while (1) {
                USBDevice::Transfer_Status status;
                size_t txUsed;
                int token = m_usb->completeTransfer_forToken(rspToken, &status, &txUsed);
                if (token == rspToken) {
                    if ((status == USBDevice::Transfer_CANCELLED)
                            || (status == USBDevice::Transfer_NODATA)) {
                        break;
                    }
                    if (status != USBDevice::Transfer_SUCCESS) {
                        PlatMutex::ScopedLock lock(respQ_lock);
                        printf("Error: libusb returned an error while trying to read data from USB.\n");
                        readThread_running = false;
                        // Wake up waiting thread to handle the error
                        respQCond.notify_one();
                        return -1;
                    }
                    readData.used = txUsed;

                    // Check if the data is async_msg or cswp req resp
                    // Need at least 7 bytes to check resp type and since
                    // it's raw encoded data from server, resp_type in
                    // varint format. 0x80 0x20 is the encoding for CSWP_ASYNC_MSG
                    bool async_resp = ((readData.used >= 7) &&
                                       (readData.data[5] == 0x80) &&
                                       (readData.data[6] == 0x20));

                    if (async_resp) {
                        PlatMutex::ScopedLock lock(asyncQ_lock);
                        asyncQ.push(readData);
                    } else {
                        PlatMutex::ScopedLock lock(respQ_lock);
                        respQ.push(readData);
                        respQCond.notify_one();
                    }
                    break;
                }
            }
        }
    } catch (...) {
        PlatMutex::ScopedLock lock(respQ_lock);
        readThread_running = false;
        respQCond.notify_one();
        return -1;
    }
    return CSWP_SUCCESS;
}

//--------------------------------------------------------------------
// CSWPBase::receive
//
// Reads from CSWP response queue and sends the data to client.
// Since every request is expected to get a response before next can
// be issued, client waits here until it sees a response in the
// respQ.
//--------------------------------------------------------------------
int
CSWPBase::receive(void* data, size_t size, size_t* used) {

    if (m_transport == "usb") {
        PlatMutex::ScopedLock lock(respQ_lock);
        while (respQ.empty() && readThread_running) {
            respQCond.wait(lock);
        }
        // If receive thread exited, we must bail out
        if (!readThread_running) {
            throw USBException("receive thread exited.");
            return CSWP_COMMS;
        }
    
        usbdata resp = respQ.front();
        respQ.pop();

        // Extract data and used for return
        uint8_t* arr = (uint8_t*)data;
        for (size_t i = 0; i < resp.used; i++) { *(arr+i) = resp.data[i]; }
        *used = resp.used;
    } else {
        size_t bytes_received = ::recv(m_socket_fd, data, size, 0);
        if (bytes_received > size) {
            throw CDebugEx(CSWP_COMMS, "Received bytes are not same as requested.");
            return CSWP_COMMS;
        }
        *used = bytes_received;
    }

    return CSWP_SUCCESS;
}

//--------------------------------------------------------------------
// CSWPBase::receive_async
//
// Reads from async response queue and sends data to client.
//--------------------------------------------------------------------
int
CSWPBase::receive_async(void* data, size_t size, size_t* used) {

    if (m_transport == "usb") {
        PlatMutex::ScopedLock lock(asyncQ_lock);
        if (asyncQ.empty()) { 
            return CSWP_ASYNC_MSG_END;
        }

        usbdata resp = asyncQ.front();
        asyncQ.pop();

        // Extract data and used for return
        uint8_t* arr = (uint8_t*)data;
        for (size_t i = 0; i < resp.used; i++) { *(arr+i) = resp.data[i]; }
        *used = resp.used;

        return CSWP_ASYNC_MSG_LOG;
    } else {
        return CSWP_ASYNC_MSG_END;
    }
}

//--------------------------------------------------------------------
// cswp_transport_connect
//--------------------------------------------------------------------
static int
cswp_transport_connect(cswp_client_t* client) {
    try {
        CSWPBase* clientbase = reinterpret_cast<CSWPBase*>(client->transport_owner);
        return clientbase->connect();
    } catch (const std::exception& e) {
        return cswp_client_error(client, CSWP_COMMS, e.what());
    }
}

//--------------------------------------------------------------------
// cswp_transport_disconnect
//--------------------------------------------------------------------
static int
cswp_transport_disconnect(cswp_client_t* client) {
    if (client->transport_owner) {
        CSWPBase* clientbase = (reinterpret_cast<CSWPBase*>(client->transport_owner));
        // Do not clear transport_owner which is set in constructor, not connect
        try {
            clientbase->disconnect();
        } catch (const std::exception& e) {
            return cswp_client_error(client, CSWP_COMMS, e.what());
        }
    }
    return CSWP_SUCCESS;
}

//--------------------------------------------------------------------
// cswp_transport_send
//--------------------------------------------------------------------
static int
cswp_transport_send(cswp_client_t* client,
                    const void* data, size_t size) {

    CSWPBase* clientbase = reinterpret_cast<CSWPBase*>(client->transport_owner);
    try {
        return clientbase->send(data, size);
    } catch (const std::exception& e) {
        return cswp_client_error(client, CSWP_COMMS, e.what());
    }
}

//--------------------------------------------------------------------
// cswp_transport_receive
//--------------------------------------------------------------------
static int
cswp_transport_receive(cswp_client_t* client,
                       void* data, size_t size, size_t* used) {

    CSWPBase* clientbase = reinterpret_cast<CSWPBase*>(client->transport_owner);
    try {
        return clientbase->receive(data, size, used);
    } catch (const std::exception& e) {
        return cswp_client_error(client, CSWP_COMMS, e.what());
    }
}

//--------------------------------------------------------------------
// cswp_transport_receive_async
//--------------------------------------------------------------------
static int
cswp_transport_receive_async(cswp_client_t* client,
                             void* data, size_t size, size_t* used) {

    CSWPBase* clientbase = reinterpret_cast<CSWPBase*>(client->transport_owner);
    try {
        return clientbase->receive_async(data, size, used);
    } catch (const std::exception& e) {
        return cswp_client_error(client, CSWP_COMMS, e.what());
    }
}


//--------------------------------------------------------------------
// init_transport_info
//--------------------------------------------------------------------
void
CSWPBase::init_transport_info(transport_cfg_t transport_cfg) {

    m_transport = transport_cfg.type;

    if (m_transport != "usb" && m_transport != "tcp") {
        throw CDebugEx(CSWP_COMMS, "Unsupported transport type.");
    }

    if (m_transport == "usb") {
        m_serialNumber = transport_cfg.serial;
        m_vendor_id    = transport_cfg.vid;
        m_product_id   = transport_cfg.pid;
        m_interface_id = transport_cfg.iid;
        m_speed_id     = transport_cfg.sid;
    } else {
        // TCP
        struct in_addr ip = {0};
        if (!inet_pton(AF_INET, transport_cfg.ipaddr.c_str(), &ip)) {
            throw CDebugEx(CSWP_COMMS, "Invalid IPv4 address for TCPDevice");
        }

        if (transport_cfg.portid <= 0 || transport_cfg.portid > 65535) {
            throw CDebugEx(CSWP_COMMS, "Invalid network port for TCPDevice");
        }

        char port_str[6]; // Convert port number to string
        sprintf(port_str, "%d", transport_cfg.portid);

        // Get address information
        struct addrinfo hints = {0};
        hints.ai_family   = AF_INET;     // IPv4
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_protocol = IPPROTO_TCP;

        int status = ::getaddrinfo(transport_cfg.ipaddr.c_str(), port_str, &hints, &m_server_info);

        if (status != 0) {
            throw CDebugEx(CSWP_COMMS, "getaddrinfo error");
        }
    }
}

//--------------------------------------------------------------------
// init_client_info
//--------------------------------------------------------------------
void
CSWPBase::init_client_info(void) {

    m_cswpClient.transport_owner = this;

    m_cswpClient.connect         = cswp_transport_connect;
    m_cswpClient.disconnect      = cswp_transport_disconnect;
    m_cswpClient.send            = cswp_transport_send;
    m_cswpClient.receive         = cswp_transport_receive;
    m_cswpClient.receive_async   = cswp_transport_receive_async;
    
    int res = cswp_client_init(&m_cswpClient);
    if (res != CSWP_SUCCESS) {
        throw CDebugEx(res, "Error while allocating memory for client.");
    }    
}

//-------------------------------------------------------------------
// CreateCSWP
//-------------------------------------------------------------------
CSWPBase*
CreateCSWP(transport_cfg_t transport_cfg) {
    CSWPBase* cswpbase = new CSWPBase(transport_cfg);
    return cswpbase;
}

//-------------------------------------------------------------------
// Wrapper functions for CSWP core functions
//-------------------------------------------------------------------
int CSWPBase::cswp_init(const char* clientID, uint64_t ProtocolVersion, uint64_t* serverProtocolVersion,
                        char* serverID, size_t serverIDSize, unsigned* serverVersion) {
    return ::cswp_init(&m_cswpClient, clientID, ProtocolVersion, serverProtocolVersion,
                       serverID, serverIDSize, serverVersion);
}
int CSWPBase::cswp_term(void) { return ::cswp_term(&m_cswpClient); }
int CSWPBase::cswp_batch_begin(int abortOnError) { return ::cswp_batch_begin(&m_cswpClient, abortOnError); }
int CSWPBase::cswp_batch_end(unsigned* opsCompleted) { return ::cswp_batch_end(&m_cswpClient, opsCompleted); }
int CSWPBase::cswp_client_info(const char* message)  { return ::cswp_client_info(&m_cswpClient, message); }
int CSWPBase::cswp_set_devices(unsigned deviceCount, const char** deviceList, const char** deviceTypes) {
    return ::cswp_set_devices(&m_cswpClient, deviceCount, deviceList, deviceTypes);
}
int CSWPBase::cswp_get_devices(unsigned* deviceCount, char** deviceList, size_t deviceListSize,
                               size_t deviceListEntrySize, char** deviceTypes, size_t deviceTypeSize,
                               size_t deviceTypeEntrySize) {
    return ::cswp_get_devices(&m_cswpClient, deviceCount, deviceList, deviceListSize,
                              deviceListEntrySize, deviceTypes, deviceTypeSize,
                              deviceTypeEntrySize);
}
int CSWPBase::cswp_get_system_description(unsigned* descriptionFormat, unsigned* descriptionSize,
                                          uint8_t* descriptionDataBuffer, size_t bufferSize) {
    return ::cswp_get_system_description(&m_cswpClient, descriptionFormat, descriptionSize,
                                         descriptionDataBuffer, bufferSize);
}
int CSWPBase::cswp_device_open(unsigned deviceNo, char* deviceInfo, size_t deviceInfoSize) {
    return ::cswp_device_open(&m_cswpClient, deviceNo, deviceInfo, deviceInfoSize);
}
int CSWPBase::cswp_device_close(unsigned deviceNo) { return ::cswp_device_close(&m_cswpClient, deviceNo); }
int CSWPBase::cswp_set_config(varint_t deviceNo, const char* name, const char* value) {
    return ::cswp_set_config(&m_cswpClient, deviceNo, name, value);
}
int CSWPBase::cswp_get_config(varint_t deviceNo, const char* name, char* value, size_t valueSize) {
    return ::cswp_get_config(&m_cswpClient, deviceNo, name, value, valueSize);
}
int CSWPBase::cswp_get_device_capabilities(varint_t deviceNo, unsigned* capabilities, unsigned* capabilityData) {
    return ::cswp_get_device_capabilities(&m_cswpClient, deviceNo, capabilities, capabilityData);
}
int CSWPBase::cswp_device_reg_list(unsigned deviceNo, unsigned* registerCount,
                                   cswp_register_info_t* registerInfo, size_t registerInfoSize,
                                   char *strBuf, size_t strBufSize) {
    return ::cswp_device_reg_list(&m_cswpClient, deviceNo, registerCount, registerInfo,
                                  registerInfoSize, strBuf, strBufSize);
}
int CSWPBase::cswp_device_reg_read(unsigned deviceNo, size_t registerCount, const uint64_t* registerIDs,
                                   uint32_t* registerValues, size_t registerValuesSize) {
    return ::cswp_device_reg_read(&m_cswpClient, deviceNo, registerCount, registerIDs,
                                  registerValues, registerValuesSize);
}
int CSWPBase::cswp_device_reg_write(unsigned deviceNo, size_t registerCount, const uint64_t* registerIDs,
                                    const uint32_t* registerValues, size_t registerValuesSize) {
    return ::cswp_device_reg_write(&m_cswpClient, deviceNo, registerCount, registerIDs,
                                   registerValues, registerValuesSize);
}
int CSWPBase::cswp_device_mem_read(unsigned deviceNo, uint64_t address, size_t size,
                                   cswp_access_size_t accessSize, unsigned flags, uint8_t* buf, size_t* bytesRead) {
    return ::cswp_device_mem_read(&m_cswpClient, deviceNo, address, size, accessSize, flags, buf, bytesRead);
}
int CSWPBase::cswp_device_mem_write(unsigned deviceNo, uint64_t address, size_t size,
                                    cswp_access_size_t accessSize, unsigned flags, const uint8_t* buf) {
    return ::cswp_device_mem_write(&m_cswpClient, deviceNo, address, size, accessSize, flags, buf);
}
int CSWPBase::cswp_device_mem_poll(unsigned deviceNo, uint64_t address, size_t size,
                                   cswp_access_size_t accessSize, unsigned flags, unsigned tries, unsigned interval,
                                   const uint8_t* mask, const uint8_t* value, uint8_t* buf, size_t* bytesRead) {
    return ::cswp_device_mem_poll(&m_cswpClient, deviceNo, address, size, accessSize, flags, tries,
                                  interval, mask, value, buf, bytesRead);
}
int CSWPBase::cswp_async_message(varint_t* deviceNo, varint_t* msg_level, char* msg, size_t msg_size) {
    return ::cswp_async_message(&m_cswpClient, deviceNo, msg_level, msg, msg_size);
}
const char *CSWPBase::cswp_decode_error(int e) {
    return ::cswp_decode_error(&m_cswpClient, e);
}
std::string CSWPBase::get_client_error(void) { return std::string(m_cswpClient.errorMsg); }
