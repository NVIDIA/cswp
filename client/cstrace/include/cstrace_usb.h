/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSTRACE_USB_H
#define CSTRACE_USB_H

#include "cstrace_base.h"
#include "usb_device.h"

//--------------------------------------------------------
// class StreamingTraceUSB
//
// Base implementation of streaming trace over USB
// Uses uddabs library to collect data from USB endpoints
//--------------------------------------------------------
class StreamingTraceUSB : public StreamingTraceBase {
public:
    StreamingTraceUSB();
    virtual ~StreamingTraceUSB();

protected:
    virtual void doConnect();
    virtual void doDisconnect();
    virtual bool isConnected();

    virtual void submitBuffer(int sink, Buffer& buf);
    virtual void doCancelPendingBuffers(int sink);
    virtual bool waitForBuffer(int sink, Buffer& buf);

    virtual std::vector<const USBDeviceIdentifier*> getDeviceIDs() = 0;
    virtual std::string                             getTargetIdentifier() = 0;
    virtual std::vector<SinkInfo>                   discoverSinks() = 0;
    virtual int                                     getControlToken() = 0;

    virtual void usbSetup();
    virtual void usbTeardown();

    virtual bool completeUsbTransfer(int sink, int token, USBDevice::Transfer_Status status, size_t used);

    std::unique_ptr<USBDevice> m_usb;
    std::queue<int>            m_dataTransferTokens;
};
#endif // CSTRACE_USB_H
