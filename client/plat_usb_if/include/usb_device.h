/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdexcept>
#include <stdint.h>
#include <memory>
#include <vector>

//---------------------------------------------
// Class USBException
//
// Throw an exception on USB error
//---------------------------------------------
class USBException : public std::runtime_error {
public:
#ifdef DEBUG
    USBException(const char* msg)        : std::runtime_error(msg) {printf("USBException: %s\n", msg);}
    USBException(const std::string& msg) : std::runtime_error(msg) {printf("USBException: %s\n", msg.c_str());}
#else
    USBException(const char* msg)        : std::runtime_error(msg) {}
    USBException(const std::string& msg) : std::runtime_error(msg) {}
#endif
};

//---------------------------------------------
// Structure USBDeviceIdentifier
//
// platform specific identifier for USB devices
//
// Linux identifies USB devices with
// Vendor ID / Product ID / interface number
//---------------------------------------------
struct USBDeviceIdentifier {
    USBDeviceIdentifier(int v, int p, int i, int d) : vendorID(v), productID(p), interfaceNumber(i), deviceVersion(d) {}
    virtual ~USBDeviceIdentifier() {};

    int vendorID;
    int productID;
    int interfaceNumber;
    int deviceVersion;   // selects between USB2 (1) and USB3 (2) device, 0 picks first found
};

//---------------------------------------------
// structure USBEPInfo 
//
// Stores USB Endpoint information
//---------------------------------------------
struct USBEPInfo {
    enum Type {                 // Type
        EP_TYPE_CONTROL,
        EP_TYPE_ISOCHRONOUS,
        EP_TYPE_BULK,
        EP_TYPE_INTERRUPT,
    };
    enum Dir { // Direction of bulk & interrupt endpoints
        EP_DIR_OUT  = 0x00, // From host to device
        EP_DIR_IN   = 0x80, // From device to host
        EP_DIR_MASK = 0x80, // Mask to extract direction from address
    };

    Type type;
    int addr;  // [7]: Direction [6:0]:EP number
};

//---------------------------------------------
// Class USBDevice
//
// Abstract class that can be used to create
// platform specific USB device interface.
//---------------------------------------------
class USBDevice {
public:
    // Control transfer flags
    // Used to form the reqType parameter of controlTransfer()
    enum Control_Transfer {
        CONTROL_RECIPIENT_DEVICE    = 0,        // Recipient is a device
        CONTROL_RECIPIENT_INTERFACE = 1,        // Recipient is an interface
        CONTROL_RECIPIENT_ENDPOINT  = 2,        // Recipient is an endpoint
        CONTROL_RECIPIENT_OTHER     = 3,        // Recipient is other
        CONTROL_TYPE_STANDARD       = (0 << 5), // Standard request
        CONTROL_TYPE_CLASS          = (1 << 5), // Class request
        CONTROL_TYPE_VENDOR         = (2 << 5), // Vendor specific request
        CONTROL_DIR_IN              = (1 << 7), // Transfer from device to host
        CONTROL_DIR_OUT             = (0 << 7), // Transfer from host to device
    };

    enum Transfer_Status {
        Transfer_SUCCESS,
        Transfer_CANCELLED,
        Transfer_ERROR,
        Transfer_IN_PROGRESS,
        Transfer_NODATA,
    };

    static std::unique_ptr<USBDevice> create (const USBDeviceIdentifier* deviceID, const std::string& serialNumber);
    virtual ~USBDevice()  {};

    virtual void                   connect() = 0;
    virtual void                   disconnect() = 0;
    virtual std::vector<USBEPInfo> getEndPoints() = 0;

    /** Get the recommended size for async transfers */
    virtual size_t                 asyncTransferSize() const = 0;
    
    /** Get the recommended number of simultaneous async transfers */
    virtual size_t                 asyncTransferCount() const = 0;

    /**
     * Control transfer
     *
     * @param reqType The type of request
     * @param request Request number
     * @param value Value to send
     * @param index Index to access
     * @param data Data to send
     * @param size Size of data
     * @param timeout Timeout
     */
    virtual int controlTransfer(uint8_t reqType, uint8_t request,
                                uint16_t value, uint16_t index,
                                void* data, uint16_t size, int timeout) = 0;

    /**
     * Start read request on bulk IN or interrupt IN endpoint
     *
     * @param endpoint Endpoint address
     * @param data Buffer to read into
     * @param size Number of bytes to read
     * @return A unique token for the transfer
     */
    virtual int submitReadTransfer(int endpoint, void* data, size_t size) = 0;

    /**
     * Start read request on bulk IN or interrupt IN endpoint
     * Variant that can be expected to be pending forever since many transfers
     * will be queued ahead of any data coming in.
     *
     * @param endpoint Endpoint address
     * @param data Buffer to read into
     * @param size Number of bytes to read
     * @return A unique token for the transfer
     */
    virtual int submitQueuedReadTransfer(int endpoint, void* data, size_t size) = 0;

    /**
     * Start write request on bulk OUT or interrupt OUT endpoint
     *
     * @param endpoint Endpoint address
     * @param data Buffer to read into
     * @param size Number of bytes to read
     * @return A unique token for the transfer
     */
    virtual int submitWriteTransfer(int endpoint, const void* data, size_t size) = 0;

    /**
     * Return number of in-progress transfers
     *
     * @return number of submitted transfers that have not been completed
     */
    virtual size_t pendingTransfers() = 0;

    /**
     * Cancel in progress transfers
     *
     * Caller should call completeTransfer to return ownership of the buffers
     */
    virtual void cancelTransfers() = 0;

    /**
     * Complete a transfer
     *
     * Return when the next read / write request completes.  The returned
     * token identifies the transfer completed to the caller
     *
     * @param status Receives the transfer status
     * @param used Receives the number of bytes transferred
     * @return Token identifying the completed transfer
     */
    virtual int completeTransfer(Transfer_Status* status, size_t* used) = 0;

    /**
     * Complete a transfer for a given token
     *
     * Return when the next read / write request with given token completes. 
     * The returned token identifies the transfer completed to the caller
     *
     * @param status Receives the transfer status
     * @param used Receives the number of bytes transferred
     * @return Token identifying the completed transfer
     */
    virtual int completeTransfer_forToken(int token, Transfer_Status* status, size_t* used) = 0;

protected:
    USBDevice()  {};

};

#endif // USB_DEVICE_H