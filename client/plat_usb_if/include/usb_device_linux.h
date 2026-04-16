/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef USB_DEVICE_LINUX_H
#define USB_DEVICE_LINUX_H

#include "plat_sync_prim.h"
#include "usb_device.h"

#include <vector>
#include <deque>
#include <queue>
#include <map>
#include <libusb.h>


//-----------------------------------------------------------------
// Class USBDeviceLinux
// 
// Implementation of USBDevice interface class for Linux using
// libusb library API.
//-----------------------------------------------------------------
class USBDeviceLinux : public USBDevice {
public:
    USBDeviceLinux(const USBDeviceIdentifier* devDesc, const std::string& serialNumber);
    virtual ~USBDeviceLinux();

    virtual void   connect();
    virtual void   disconnect();
    virtual std::vector<USBEPInfo> getEndPoints();
    virtual size_t asyncTransferSize() const;
    virtual size_t asyncTransferCount() const;
    virtual int    controlTransfer(uint8_t reqType, uint8_t request,
                                   uint16_t value, uint16_t index,
                                   void* data, uint16_t size, int timeout);
    virtual int    submitReadTransfer(int endpoint, void* data, size_t size);
    // Variant that can be expected to be pending forever since many transfers
    // will be queued ahead of any data coming in.
    virtual int    submitQueuedReadTransfer(int endpoint, void* data, size_t size);
    virtual int    submitWriteTransfer(int endpoint, const void* data, size_t size);
    virtual size_t pendingTransfers();
    virtual void   cancelTransfers();
    virtual int    completeTransfer(Transfer_Status* status, size_t* used);
    virtual int    completeTransfer_forToken(int token, Transfer_Status* status, size_t* used);
    //Callback on completed transfer
    void transfer_complete(libusb_transfer* transfer);

private:
    // Transfer information
    struct Transfer {
        int token;                 // Token allocated when submitting transfer
        libusb_transfer* transfer; // libusb transfer instance
    };
    typedef std::queue<Transfer> TransferQueue;
    typedef std::deque<Transfer> TransferDeque;

    void                  doDisconnect();
    libusb_device_handle* findAndOpenDeviceBySerialNumber();
    void                  examineEndpoints();
    Transfer              submitTransfer(uint8_t address, USBEPInfo::Dir dir, void* data,
                                         size_t len, uint32_t wait_msecs);
    libusb_transfer*      createTransfer(const USBEPInfo& ep, void* data, size_t len,
                                         uint32_t wait_msecs);
    static int            getUsbString(libusb_device_handle *devHandle, uint8_t index, uint16_t langId,
                                       std::string& str);

    PlatMutex                        m_lock;
    int                              m_vendorID;
    int                              m_productID;
    int                              m_deviceVersion;
    int                              m_interfaceNumber;
    int                              m_altSetting;
    std::string                      m_serialNumber;
    libusb_context                  *m_usbContext;
    libusb_device_handle*            m_usbDev;
    std::vector<USBEPInfo>           m_epInfo;
    int                              m_nextToken;
    TransferQueue                    m_queuedTransfers;
    std::map<uint8_t, TransferDeque> m_epInFlightTransfers;
    size_t                           m_inFlightTransfers;
    TransferQueue                    m_completedTransfers;
    int                              m_completedFlag;
};

#endif // USB_DEVICE_LINUX_H
