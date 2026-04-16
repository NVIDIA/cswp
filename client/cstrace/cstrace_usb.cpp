/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#include "cstrace_usb.h"
#include "cstrace_ex.h"
#include "cstrace_types.h"

//--------------------------
// StreamingTraceUSB
// Constructor, Destructor
//--------------------------
StreamingTraceUSB::StreamingTraceUSB() : m_usb() {}
StreamingTraceUSB::~StreamingTraceUSB() {}

//----------------------------------------
// StreamingTraceUSB::doConnect
//----------------------------------------
void
StreamingTraceUSB::doConnect() {

    std::vector<const USBDeviceIdentifier*> deviceIDs = getDeviceIDs();

    if (deviceIDs.empty()) {
        throw CSTraceEx(CSTRACE_NO_DEVICE, "No device identifiers defined");
    }
    if (deviceIDs[0] == nullptr) {
        throw CSTraceEx(CSTRACE_NO_DEVICE, "Invalid device identifier");
    }
    m_usb = USBDevice::create(deviceIDs[0], getTargetIdentifier());
    if (!m_usb.get()) {
        throw CSTraceEx(CSTRACE_NO_DEVICE, "USBDevice::create failed");
    }
    try {
        m_usb->connect();
        usbSetup();
    } catch (...) {
        // Device not accessible (e.g. already in use, claim failed).  Clear
        // m_usb so we do not leave a half-connected object; otherwise a later
        // discoverSinks() could dereference it and crash.
        m_usb.reset();
        throw;
    }
}

//----------------------------------------
// StreamingTraceUSB::doDisconnect
//----------------------------------------
void
StreamingTraceUSB::doDisconnect() {

    usbTeardown();
    if (m_usb.get()) {
        m_usb->disconnect();
        m_usb.reset(0);
    }
}

//----------------------------------------
// StreamingTraceUSB::isConnected
//
// called with lock held
//----------------------------------------
bool
StreamingTraceUSB::isConnected() {
    return (m_usb.get() != 0);
}

//----------------------------------------
// StreamingTraceUSB::usbSetup
//----------------------------------------
void
StreamingTraceUSB::usbSetup() {
    // no action here - implementations may override for device specific setup
    // (e.g. alternate interface selection, control transfers etc)
}

//----------------------------------------
// StreamingTraceUSB::usbTeardown
//----------------------------------------
void
StreamingTraceUSB::usbTeardown() {
    // no action here - implementations may override for device specific teardown
    // (e.g. alternate interface selection, control transfers etc)
}

//----------------------------------------
// StreamingTraceUSB::submitBuffer
//----------------------------------------
void
StreamingTraceUSB::submitBuffer(int sink, Buffer& buf) {

    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
    unsigned int endpoint = sinkState->transportID;
    int token = m_usb->submitQueuedReadTransfer((int)endpoint,
                                          buf.pEventBuffer->pBuf,
                                          buf.pEventBuffer->size);
    m_dataTransferTokens.push(token);
}

//--------------------------------------------
// StreamingTraceUSB::doCancelPendingBuffers
//--------------------------------------------
void
StreamingTraceUSB::doCancelPendingBuffers(int sink __attribute__((unused))) {
    m_usb->cancelTransfers();
}

//----------------------------------------
// StreamingTraceUSB::waitForBuffer
//----------------------------------------
bool
StreamingTraceUSB::waitForBuffer(int sink, Buffer& buf) {
    USBDevice::Transfer_Status status;
    size_t                     used;

    // Loop until we get a data token or encounter an error. Process any
    // incoming metadata control message while doing so.

    while (true) {
        // release lock during blocking operation
        m_lock.unlock();

        int token = m_usb->completeTransfer(&status, &used);
        m_lock.lock();

        int expCtrlToken  = getControlToken();
        int expDataToken = m_dataTransferTokens.empty() ? -1 : m_dataTransferTokens.front();

        if (token == -1) { 
            return false;
        } else if (token == expCtrlToken) {
            bool ctrl_msg_handled = completeUsbTransfer(sink, token, status, used);
            if (!ctrl_msg_handled) {
                // Although this check is not necessary, it is better to fail
                // early than debug odd usb errors later.
                return false;
            }
            // Do not exit. Continue looping to wait for data token
        } else if (token == expDataToken) {
            m_dataTransferTokens.pop();
            CSTraceEventBuffer* buffer = buf.pEventBuffer;
            switch (status) {
                case USBDevice::Transfer_SUCCESS:
                case USBDevice::Transfer_IN_PROGRESS:
                    buffer->type = CSTRACE_EVENT_TYPE_DATA;
                    buffer->used = used;
                    break;

                case USBDevice::Transfer_CANCELLED:
                    buffer->type = CSTRACE_EVENT_TYPE_END_OF_DATA;
                    buffer->used = 0;
                    break;

                case USBDevice::Transfer_NODATA:
                    // return empty data packet
                    buffer->type = CSTRACE_EVENT_TYPE_DATA;
                    buffer->used = 0;
                    break;

                case USBDevice::Transfer_ERROR:
                    buffer->type = CSTRACE_EVENT_TYPE_ERROR;
                    buffer->used = 0;
                    break;
            }
            return true;
        } else {
            // Expectation is that when this functon is called, there is at
            // least a control or a data req pending. If the incoming token
            // corresponds to neither, it's an unknown token and unexpected
            // error.
            return false;
        }
    }
}

//----------------------------------------
// StreamingTraceUSB::completeUsbTransfer
//----------------------------------------
bool
StreamingTraceUSB::completeUsbTransfer(int sink __attribute__((unused)),
                                       int token __attribute__((unused)),
                                       USBDevice::Transfer_Status status __attribute__((unused)),
                                       size_t used __attribute__((unused))) {
    // handler for implementation defined USB transfers
    return false;
}
