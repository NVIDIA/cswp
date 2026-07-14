/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#include "usb_device_linux.h"
#include <cstring>

namespace {
    const int DEFAULT_CONFIGURATION_INDEX = 0;
    const size_t MAX_URB_SIZE = 16384; // Minimum required size is 16kB as cmd and rsp buffers are sized for 16kB.
    const size_t MAX_IN_FLIGHT = 16;   // Arbitrary number as libusb does not have a limit, make it more than CSWP requires.
}

//-----------------------------------------------------------------------------
// USBDeviceLinux class
//
// Constructor and destructor
//-----------------------------------------------------------------------------
USBDeviceLinux::USBDeviceLinux(const USBDeviceIdentifier* devDesc, const std::string& serialNumber)
    : m_vendorID(devDesc->vendorID),
      m_productID(devDesc->productID),
      m_deviceVersion(devDesc->deviceVersion),
      m_interfaceNumber(devDesc->interfaceNumber),
      m_altSetting(0),
      m_serialNumber(serialNumber),
      m_usbContext(0),
      m_usbDev(0),
      m_nextToken(0),
      m_inFlightTransfers(0),
      m_completedFlag(0)
{}

USBDeviceLinux::~USBDeviceLinux() { disconnect(); }

//-----------------------------------------------------------------------------
// USBDeviceLinux::connect
//-----------------------------------------------------------------------------
void
USBDeviceLinux::connect() {
    PlatMutex::ScopedLock lock(m_lock);
    int err = 0;

    if (m_usbContext == 0) {
        err = libusb_init(&m_usbContext);
        if (err != 0) {
            m_usbContext = 0;
            throw USBException("Error initialising libusb");
        }
    }

    // libusb_set_debug(m_usbContext, LIBUSB_LOG_LEVEL_DEBUG);

    // search by serial number for multiple devices
    m_usbDev = findAndOpenDeviceBySerialNumber();
    if (m_usbDev == 0) {
        doDisconnect();
        throw USBException("Error opening device");
    }

    // claim the interface
    if (libusb_claim_interface(m_usbDev, m_interfaceNumber) < 0) {
        throw USBException("Error claiming device interface");
    }

    // build endpoint list
    examineEndpoints();

    // reset bulk endpoints to clear toggle bits
    for (std::vector<USBEPInfo>::const_iterator e = m_epInfo.begin();
         e != m_epInfo.end();
         ++e) {
        if (e->type == USBEPInfo::EP_TYPE_BULK || e->type == USBEPInfo::EP_TYPE_BULK) {
            if (libusb_clear_halt(m_usbDev, e->addr) < 0) {
                throw USBException("Failed to clear endpoints");
            }
        }
    }
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::disconnect
//-----------------------------------------------------------------------------
void
USBDeviceLinux::disconnect() {
    PlatMutex::ScopedLock lock(m_lock);
    doDisconnect();
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::getEndPoints
//-----------------------------------------------------------------------------
std::vector<USBEPInfo>
USBDeviceLinux::getEndPoints() {
    PlatMutex::ScopedLock lock(m_lock);
    // return information gathered on connect
    return m_epInfo;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::asyncTransferSize
//-----------------------------------------------------------------------------
size_t
USBDeviceLinux::asyncTransferSize() const { return MAX_URB_SIZE; }

//-----------------------------------------------------------------------------
// USBDeviceLinux::asyncTransferCount
//-----------------------------------------------------------------------------
size_t
USBDeviceLinux::asyncTransferCount() const { return MAX_IN_FLIGHT; }

//-----------------------------------------------------------------------------
// USBDeviceLinux::controlTransfer
//-----------------------------------------------------------------------------
int
USBDeviceLinux::controlTransfer(uint8_t reqType, uint8_t request,
                                uint16_t value, uint16_t index,
                                void* data, uint16_t size, int timeout) {
    // No need for lock here as internal state is not used
    int res = libusb_control_transfer(m_usbDev,
                                      reqType, request,
                                      value, index,
                                      reinterpret_cast<uint8_t*>(data), size,
                                      timeout);

    return res; // number of bytes received
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::submitReadTransfer
//-----------------------------------------------------------------------------
int
USBDeviceLinux::submitReadTransfer(int endpoint, void* data, size_t size) {
    PlatMutex::ScopedLock lock(m_lock);

    Transfer transfer = submitTransfer(endpoint, USBEPInfo::EP_DIR_IN,
                                       data, size, 1000);
    return transfer.token;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::submitQueuedReadTransfer
//-----------------------------------------------------------------------------
int
USBDeviceLinux::submitQueuedReadTransfer(int endpoint, void* data, size_t size) {
    PlatMutex::ScopedLock lock(m_lock);

    Transfer transfer = submitTransfer(endpoint, USBEPInfo::EP_DIR_IN,
                                       data, size, 0);
    return transfer.token;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::submitWriteTransfer
//-----------------------------------------------------------------------------
int
USBDeviceLinux::submitWriteTransfer(int endpoint, const void* data, size_t size) {
    PlatMutex::ScopedLock lock(m_lock);

    Transfer transfer = submitTransfer(endpoint, USBEPInfo::EP_DIR_OUT,
                                       const_cast<void*>(data), size, 1000);
    return transfer.token;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::pendingTransfers
//-----------------------------------------------------------------------------
size_t
USBDeviceLinux::pendingTransfers() {
    PlatMutex::ScopedLock lock(m_lock);

    return m_queuedTransfers.size() + m_inFlightTransfers + m_completedTransfers.size();
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::cancelTransfers
//-----------------------------------------------------------------------------
void
USBDeviceLinux::cancelTransfers() {
    PlatMutex::ScopedLock lock(m_lock);

    // cancel any transfers submitted to libusb
    for (std::map<uint8_t, TransferDeque>::iterator epInflight = m_epInFlightTransfers.begin();
         epInflight != m_epInFlightTransfers.end();
         ++epInflight) {
        for (TransferDeque::iterator t = epInflight->second.begin();
             t != epInflight->second.end();
             ++t) {
            libusb_cancel_transfer(t->transfer);
        }
    }

    // move queued buffers straight to completed list will be free'd in completeTransfer()
    while (!m_queuedTransfers.empty()) {
        Transfer t = m_queuedTransfers.front();
        m_queuedTransfers.pop();
        t.transfer->status = LIBUSB_TRANSFER_CANCELLED;
        m_completedTransfers.push(t);
    }
}

//-----------------------------------------
// map_libusb_status
//-----------------------------------------
static USBDevice::Transfer_Status
map_libusb_status(int libusb_status) {
    switch (libusb_status) {
        case LIBUSB_TRANSFER_COMPLETED:
            return USBDevice::Transfer_SUCCESS;
        case LIBUSB_TRANSFER_CANCELLED:
            return USBDevice::Transfer_CANCELLED;
        case LIBUSB_TRANSFER_TIMED_OUT:
            return USBDevice::Transfer_NODATA;
        case LIBUSB_TRANSFER_STALL:
            return USBDevice::Transfer_CANCELLED;
        case LIBUSB_TRANSFER_NO_DEVICE:
        case LIBUSB_TRANSFER_OVERFLOW:
        case LIBUSB_TRANSFER_ERROR:
        default:
            return USBDevice::Transfer_ERROR;
    }
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::completeTransfer_forToken
// This API does not return until the token has been found.
//-----------------------------------------------------------------------------
int
USBDeviceLinux::completeTransfer_forToken(int token, Transfer_Status* status, size_t* used) {
    while (1) {
        m_lock.lock();
        // A list might be more appropriate here because elements can be
        // out of order.
        for (size_t i = 0; i < m_completedTransfers.size(); i++) {
            Transfer transfer = m_completedTransfers.front();
            m_completedTransfers.pop();
            if (transfer.token == token) {
                m_lock.unlock();
                if (used)   { *used = transfer.transfer->actual_length; }
                if (status) { *status = map_libusb_status(transfer.transfer->status); }

                libusb_free_transfer(transfer.transfer);
                return transfer.token;
            }
            // Put it back at the end
            m_completedTransfers.push(transfer);
        }
        // Transfer has not completed yet, we wait unless a tx completed flag
        // was already received.
        m_completedFlag = 0;
        m_lock.unlock(); // release lock during blocking operation
        int ret = libusb_handle_events_completed(m_usbContext, &m_completedFlag);
        if ((ret != 0) && (ret != LIBUSB_ERROR_TIMEOUT)) {
            throw USBException("Interrupted while processing USB events");
        }
    }
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::completeTransfer
//-----------------------------------------------------------------------------
int
USBDeviceLinux::completeTransfer(Transfer_Status* status, size_t* used) {
    PlatMutex::ScopedLock lock(m_lock);

    if (m_completedTransfers.empty() && (m_inFlightTransfers == 0)) {
        // Nothing in the queue and nothing to wait for
        return -1;
    }

    // wait for a transfer to complete by calling transfer_complete
    while (m_completedTransfers.empty()) {
        int ret;

        m_completedFlag = 0;
        // call libusb to service pending transfers
        m_lock.unlock(); // release lock during blocking operation
        ret = libusb_handle_events_completed(m_usbContext, &m_completedFlag);
        if (ret != 0) {
            throw USBException("Interrupted while processing USB events");
        }
        m_lock.lock();
    }

    // return the first completed transfer
    Transfer transfer = m_completedTransfers.front();
    m_completedTransfers.pop();

    if (used)   { *used = transfer.transfer->actual_length; }
    if (status) { *status = map_libusb_status(transfer.transfer->status); }

    libusb_free_transfer(transfer.transfer);

    return transfer.token;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::doDisconnect
//-----------------------------------------------------------------------------
void
USBDeviceLinux::doDisconnect() {
    if (m_usbDev != 0) {
        libusb_close(m_usbDev); // close device
        m_usbDev = 0;
    }
    if (m_usbContext != 0) {
        libusb_exit(m_usbContext); // close libusb context
        m_usbContext = 0;
    }
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::findAndOpenDeviceBySerialNumber
//-----------------------------------------------------------------------------
libusb_device_handle*
USBDeviceLinux::findAndOpenDeviceBySerialNumber() {

    libusb_device_handle* foundDevice = 0;
    // scan for device if libusb is available
    if (m_usbContext != 0) {
        // scan the bus
        libusb_device** devs;
        int numDevs = libusb_get_device_list(m_usbContext, &devs);
        for (int i = 0; i < numDevs && foundDevice == 0; ++i) {
            libusb_device_descriptor desc;
            int err = libusb_get_device_descriptor(devs[i], &desc);
            if (err == 0 &&
                desc.idVendor == m_vendorID &&
                desc.idProduct == m_productID &&
                ((desc.bcdDevice == m_deviceVersion) || (m_deviceVersion == 0))) {
                // Found a device we're interested in: probe it's interfaces
                libusb_config_descriptor *config;
                int err = libusb_get_active_config_descriptor(devs[i], &config);
                if (err == 0) {
                    for (int interfaceIndex = 0; interfaceIndex < config->bNumInterfaces && foundDevice == 0; ++interfaceIndex) {
                        // get interface number from first alternate setting
                        for (int altSetting = 0; altSetting < config->interface[interfaceIndex].num_altsetting; ++altSetting) {
                            if (config->interface[interfaceIndex].altsetting[altSetting].bInterfaceNumber == m_interfaceNumber) {
                                // try to open the device and get the serial number - ignore any failures - these could
                                // be because the device is already open somewhere else
                                libusb_device_handle* devHandle;
                                int err = libusb_open(devs[i], &devHandle);
                                if (err == 0) {
                                    std::string serial;
                                    std::string manufacturer;
                                    if (m_serialNumber.empty()) {
                                        foundDevice = devHandle; /* use first device found */
                                    }
                                    else if (getUsbString(devHandle, desc.iSerialNumber, 0, serial) > 0) {
                                        if (serial == m_serialNumber) {
                                            foundDevice = devHandle;
                                        }
                                    } else if (getUsbString(devHandle, desc.iManufacturer, 0, manufacturer) > 0) {
                                        // USB device may not have serialNumber setup correctly. Check Manufacturer as an alternative.
                                        if (manufacturer == m_serialNumber) {
                                            foundDevice = devHandle;
                                        }
                                    } else {
                                        foundDevice = 0;
                                    }
                                    // close the device if it wasn't the wanted one, otherwise leave it open for return
                                    if (foundDevice == 0) {
                                        libusb_close(devHandle);
                                    }
                                }
                                break; // only look at each interface once
                            }
                        }
                    }
                    libusb_free_config_descriptor(config);
                } else {
                    throw USBException("Failed to get active config descriptor");
                }
            }
        }
        libusb_free_device_list(devs, 1);
    } else {
        throw USBException("No libusb context");
    }
    if (!foundDevice) {
        throw USBException("CSWP device not found");
    }
    return foundDevice;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::examineEndpoints
//-----------------------------------------------------------------------------
void
USBDeviceLinux::examineEndpoints() {
    m_epInfo.clear();

    libusb_config_descriptor *config;
    libusb_device* dev = libusb_get_device(m_usbDev);
    int err = libusb_get_config_descriptor(dev, DEFAULT_CONFIGURATION_INDEX, &config);
    if (err != 0) {
        throw USBException("Failed to get endpoint information");
    }
    // find the endpoints for the target interface
    for (int interfaceIndex = 0; interfaceIndex < config->bNumInterfaces; ++interfaceIndex) {
        if (config->interface[interfaceIndex].altsetting[m_altSetting].bInterfaceNumber == m_interfaceNumber) {
            const struct libusb_endpoint_descriptor *endpoints = config->interface[interfaceIndex].altsetting[m_altSetting].endpoint;
            // get info on each endpoint
            for (int epIndex = 0; epIndex < config->interface[interfaceIndex].altsetting[m_altSetting].bNumEndpoints; ++epIndex) {
                USBEPInfo ep;
                ep.addr = endpoints[epIndex].bEndpointAddress;
                switch (endpoints[epIndex].bmAttributes & 0x3) {
                    case LIBUSB_TRANSFER_TYPE_CONTROL:     ep.type = USBEPInfo::EP_TYPE_CONTROL; break;
                    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS: ep.type = USBEPInfo::EP_TYPE_ISOCHRONOUS; break;
                    case LIBUSB_TRANSFER_TYPE_BULK:        ep.type = USBEPInfo::EP_TYPE_BULK; break;
                    case LIBUSB_TRANSFER_TYPE_INTERRUPT:   ep.type = USBEPInfo::EP_TYPE_INTERRUPT; break;
                }
                m_epInfo.push_back(ep);
            }
            break;
        }
    }
    libusb_free_config_descriptor(config);
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::submitTransfer
//
// Submit a transfer to libusb
//-----------------------------------------------------------------------------
USBDeviceLinux::Transfer
USBDeviceLinux::submitTransfer(uint8_t address, USBEPInfo::Dir dir,
                               void* data, size_t len, uint32_t wait_msecs) {
    Transfer transfer;
    transfer.token = m_nextToken++;

    // find endpoint info
    std::vector<USBEPInfo>::const_iterator epInfo = m_epInfo.end();
    for (std::vector<USBEPInfo>::const_iterator e = m_epInfo.begin();
         e != m_epInfo.end();
         ++e) {
        if (e->addr == address) { epInfo = e; }
    }
    if (epInfo == m_epInfo.end()) {
        throw USBException("Invalid endpoint address");
    }

    // create libusb transfers
    if (len > MAX_URB_SIZE) {
        throw USBException("Invalid transfer size");
    }
    if (dir != (epInfo->addr & USBEPInfo::EP_DIR_MASK)) {
        throw USBException("Invalid endpoint direction");
    }
    transfer.transfer = createTransfer(*epInfo, data, len, wait_msecs);

    // submit to libusb if space available
    TransferDeque& epTransfers = m_epInFlightTransfers[address];
    if (epTransfers.size() < MAX_IN_FLIGHT) {
        int err = libusb_submit_transfer(transfer.transfer);
        if (err < 0) {
            libusb_free_transfer(transfer.transfer);
            throw USBException("Failed to submit transfer");
        }
        epTransfers.push_back(transfer);
        ++m_inFlightTransfers;
    } else {
        // otherwise queue for submission when transfers complete
        m_queuedTransfers.push(transfer);
    }
    return transfer;
}

//-----------------------------------------------------------------------------
// cb_transfer
//
// Callback on transfer complete: call USBDeviceLinux::transfer_complete
//-----------------------------------------------------------------------------
static void
LIBUSB_CALL cb_transfer(libusb_transfer *transfer) {
    USBDeviceLinux* dev = (USBDeviceLinux*)transfer->user_data;
    dev->transfer_complete(transfer);
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::createTransfer
//
// Create a libusb transfer
//-----------------------------------------------------------------------------
libusb_transfer*
USBDeviceLinux::createTransfer(const USBEPInfo& epInfo,
                               void* data, size_t len, uint32_t wait_msecs) {

    libusb_transfer *transfer = libusb_alloc_transfer(0);
    void *cb_context = this;

    switch (epInfo.type) {
    case USBEPInfo::EP_TYPE_BULK:
        libusb_fill_bulk_transfer(transfer, m_usbDev, epInfo.addr,
                                  reinterpret_cast<uint8_t*>(data), len,
                                  cb_transfer, cb_context,
                                  wait_msecs);
        break;

    case USBEPInfo::EP_TYPE_INTERRUPT:
        libusb_fill_interrupt_transfer(transfer, m_usbDev, epInfo.addr,
                                       reinterpret_cast<uint8_t*>(data), len,
                                       cb_transfer, cb_context,
                                       wait_msecs);
        break;

    default:
        libusb_free_transfer(transfer);
        throw USBException("Unsupported endpoint type");
    }

    return transfer;
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::transfer_complete
//
// Callback to handle completion of a transfer
//-----------------------------------------------------------------------------
void
USBDeviceLinux::transfer_complete(libusb_transfer* transfer) {
    PlatMutex::ScopedLock lock(m_lock);

    // find transfer in pending set
    TransferDeque& epTransfers = m_epInFlightTransfers[transfer->endpoint];
    TransferDeque::iterator tx = epTransfers.end();
    for (TransferDeque::iterator t = epTransfers.begin();
         t != epTransfers.end();
         ++t) {
        if (t->transfer == transfer) { tx = t; }
    }

    if (tx != epTransfers.end()) {
        // move to completed queue
        m_completedTransfers.push(*tx);
        epTransfers.erase(tx);
        --m_inFlightTransfers;
        m_completedFlag = 1; // tell completeTransfer() that data is ready
    }

    // submit more transfers
    while (epTransfers.size() < MAX_IN_FLIGHT && !m_queuedTransfers.empty()) {
        Transfer t = m_queuedTransfers.front();
        m_queuedTransfers.pop();
        int err = libusb_submit_transfer(t.transfer);
        if (err < 0) {
            libusb_free_transfer(t.transfer);
            break;
        }
        epTransfers.push_back(t);
        ++m_inFlightTransfers;
    }
}

//-----------------------------------------------------------------------------
// USBDeviceLinux::getUsbString
//-----------------------------------------------------------------------------
int
USBDeviceLinux::getUsbString(libusb_device_handle *devHandle,
                             uint8_t index, uint16_t langId, std::string& str) {

    int res = 0;
    const size_t MAX_DESC = 256;
    unsigned char buf[MAX_DESC];
    memset(buf, 0, MAX_DESC);

    // lookup first supported language from descriptor 0
    if (langId == 0) {
        res = libusb_control_transfer(devHandle,
                                      (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE),
                                      LIBUSB_REQUEST_GET_DESCRIPTOR,
                                      ((LIBUSB_DT_STRING << 8) | 0), // descriptor type / index
                                      0, buf, MAX_DESC, 1000);
        if (res < 0) { return res;}

        langId = buf[2] | (buf[3] << 8);
    }

    // get string data
    res = libusb_control_transfer(devHandle,
                                  (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE),
                                  LIBUSB_REQUEST_GET_DESCRIPTOR,
                                  ((LIBUSB_DT_STRING << 8) | index), // descriptor type / index
                                  langId, buf, MAX_DESC, 1000);

    if (res > 2) {
        // descriptor returned has data length (bytes) in first byte, index in second, then unicode data (16 bits)
        // extract the length and convert to ascii (discarding high bytes)
        size_t strLen = (buf[0] - 2) / 2;
        str.resize(strLen);
        unsigned short* src = (unsigned short*)&buf[2];
        for (size_t i = 0; i < strLen; ++i, ++src) {
            str[i] = (unsigned char)(*src & 0xFF);
        }
    } else {
        res = -1; // No string data received. Try printing libusb_error_name
    }

    return res;
}
