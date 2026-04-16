/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#include "cstrace_plat_if.h"
#include "cstrace_ex.h"

#include <sstream>
#include <iomanip>
#include <stdexcept>

//-------------------------------------------------------------------
// CreateStreamingTrace
//
// factory method to create instance
//-------------------------------------------------------------------
StreamingTraceBase* 
CreateStreamingTrace(transport_cfg_t transport_cfg) {
    StreamingTraceUSBClient* st = new StreamingTraceUSBClient(transport_cfg);
    return st;
}

//-------------------------------------------------------
// StreamingTraceUSBClient
//
// Constructor and destructor
//-------------------------------------------------------
StreamingTraceUSBClient::StreamingTraceUSBClient(transport_cfg_t transport_cfg)
    : m_bufferSize(DEFAULT_ETR_BUFFER_SIZE),
      m_bufferWM(DEFAULT_ETR_WATERMARK),
      m_controlToken(-1) {

    if (transport_cfg.type != "usb") {
        throw CSTraceEx(CSTRACE_INVALID_TARGET, "Unsupported transport type. Only allowed usb.");
    }

    m_targetID = transport_cfg.serial;
    m_vid       = transport_cfg.vid;
    m_pid       = transport_cfg.pid;
    m_interface = transport_cfg.iid;
    m_speed_id  = transport_cfg.sid;

    USBDeviceIdentifier* USBID = new USBDeviceIdentifier((int)m_vid, (int)m_pid, (int)m_interface, (int)m_speed_id);
    m_usbID.reset(USBID);
}

StreamingTraceUSBClient::~StreamingTraceUSBClient() {
    // Must call Disconnect() here (most-derived destructor) so virtual
    // methods (doDisconnect, detachDevice, etc.) still resolve correctly.
    // This stops the data thread and releases USB resources before
    // base class destructors run.
    try {
        Disconnect();
    } catch (...) {
        // Ignore errors during cleanup
    }
}

//-------------------------------------------------------
// StreamingTraceUSBClient::getDeviceIDs
//-------------------------------------------------------
std::vector<const USBDeviceIdentifier*>
StreamingTraceUSBClient::getDeviceIDs() {
    std::vector<const USBDeviceIdentifier*> devIDs;
    devIDs.push_back(m_usbID.get());
    return devIDs;
}

//-------------------------------------------------------
// StreamingTraceUSBClient::discoverSinks
//-------------------------------------------------------
std::vector<StreamingTraceBase::SinkInfo>
StreamingTraceUSBClient::discoverSinks() {

    // Note:
    //
    // CSWP protocol uses control transfer requests to
    // enumerate all the ETRs and get their names from target.
    // However, in our usecase, ETRId-ETRName map can be made
    // static and can just be read from a hardcoded table.
    //
    // Use static const char* to avoid std::string copy from heap
    // across potential allocator boundaries (e.g. librddi_strace
    // vs libcstrace_client), which can cause SIGSEGV in free().

    static const char* const etr_names[] = {
        "etr-system-main",
        "etr-system-top",
        "etr-system-hsio",
        "etr-compute-top",
        "etr-mem0-top",
        "etr-mem0-io128-0",
        "etr-mem0-io128-1",
        "etr-mem1-top",
        "etr-mem1-io128-0",
        "etr-mem1-io128-1",
        "etr-mem2-top",
        "etr-mem2-io128-0",
        "etr-mem2-io128-1",
        "etr-mem3-top",
        "etr-mem3-io128-0",
        "etr-mem3-io128-1",
        "etr-c1-system-main",
        "etr-c1-system-top",
        "etr-c1-system-hsio",
        "etr-c1-compute-top",
        "etr-c1-mem0-top",
        "etr-c1-mem0-io128-0",
        "etr-c1-mem0-io128-1",
        "etr-c1-mem1-top",
        "etr-c1-mem1-io128-0",
        "etr-c1-mem1-io128-1",
        "etr-c1-mem2-top",
        "etr-c1-mem2-io128-0",
        "etr-c1-mem2-io128-1",
        "etr-c1-mem3-top",
        "etr-c1-mem3-io128-0",
        "etr-c1-mem3-io128-1",
    };
    static const unsigned num_names =
        sizeof(etr_names) / sizeof(etr_names[0]);
    const unsigned n = (MAX_NUM_ETRS < num_names) ? MAX_NUM_ETRS : num_names;

    std::vector<SinkInfo> sinks;
    sinks.reserve(n);

    for (unsigned i = 0; i < n; ++i) {
        SinkInfo sink;
        sink.details.name.assign(etr_names[i]);
        sink.details.metadata = "";
        sink.details.dataBufferCount = USB_TRACE_BUFFERS_COUNT;
        sink.details.dataBufferSize = USB_TRACE_BUFFERS_SIZE;
        sink.details.eventBufferCount = USB_TRACE_EVTBUFFERS_COUNT;
        sink.details.eventBufferSize = USB_TRACE_EVTBUFFERS_SIZE;
        sinks.push_back(sink);
    }

    return std::move(sinks);  // avoid vector copy and string copies at call site
}

//-------------------------------------------------------
// StreamingTraceUSBClient::getTargetIdentifier
//-------------------------------------------------------
std::string
StreamingTraceUSBClient::getTargetIdentifier() {
    return m_targetID;
}

//-------------------------------------------------------
// StreamingTraceUSBClient::getControlToken
//-------------------------------------------------------
int
StreamingTraceUSBClient::getControlToken() {
    return m_controlToken; 
}

//-------------------------------------------------------
// StreamingTraceUSBClient::usbSetup
//-------------------------------------------------------
void
StreamingTraceUSBClient::usbSetup() {
    // get available endpoints
    m_traceEndpoints.clear();
    std::vector<USBEPInfo> epInfo = m_usb->getEndPoints();
    for (std::vector<USBEPInfo>::const_iterator ep = epInfo.begin();
         ep != epInfo.end();
         ++ep)
    {
        if ((ep->addr & USBEPInfo::EP_DIR_MASK) == USBEPInfo::EP_DIR_IN &&
            ep->type == USBEPInfo::EP_TYPE_BULK) {
            EndpointInfo e;
            e.addr = ep->addr;
            e.attachedSink = -1;
            m_traceEndpoints.push_back(e);
        } else if ((ep->addr & USBEPInfo::EP_DIR_MASK) == USBEPInfo::EP_DIR_IN &&
                    ep->type == USBEPInfo::EP_TYPE_INTERRUPT) {
            m_controlEP = ep->addr;
        }
    }
}

//-------------------------------------------------------
// StreamingTraceUSBClient::usbTeardown
//-------------------------------------------------------
void
StreamingTraceUSBClient::usbTeardown()
{
}

//-------------------------------------------------------
// StreamingTraceUSBClient::attachDevice
//-------------------------------------------------------
void
StreamingTraceUSBClient::attachDevice(int sink) {
    // validate buffer parameters
    if (m_bufferSize % 4096 != 0)
        throw USBException("Buffer size must be a multiple of 4096");
    if (m_bufferSize > 0xFFFF000)
        throw USBException("Buffer size must be less than 0xFFFF000");
    if (m_bufferWM % 4096 != 0)
        throw USBException("Buffer watermark must be a multiple of 4096");
    if (m_bufferWM >= m_bufferSize)
        throw USBException("Buffer watermark must be less than buffer size");

    // find free trace bulk input endpoint
    std::vector<EndpointInfo>::iterator attachEP = m_traceEndpoints.end();
    for (std::vector<EndpointInfo>::iterator e = m_traceEndpoints.begin();
         e != m_traceEndpoints.end();
         ++e) {
        if (e->attachedSink == -1) {
            attachEP = e;
            break;
        }
    }

    if (attachEP == m_traceEndpoints.end())
        throw USBException("No free trace stream for ETR");

    // Configuration requests to CSWP server via USB control transfers:

    // To send the sinkId to target to collect trace from. Target will use this to program corresponding ETR
    // accordingly.
    if (endpointSetParam((uint16_t)attachEP->addr, USB_CSTRACE_EP_REQ_SET_SINK, (uint16_t)sink) < 0) {
        throw USBException("Failed to set ETR endpoint");
    }
    // To set the timeout before partially filled buffers are sent to the host
    if (endpointSetParam((uint16_t)attachEP->addr, USB_CSTRACE_EP_REQ_SET_TX_TIMEOUT, ETR_TX_TIMEOUT) < 0) {
        throw USBException("Failed to set ETR endpoint timeout");
    }
    // To set sink buffer size, the unit is 4k blocks
    uint16_t bufferBlocks = (uint16_t)((m_bufferSize / 4096) & 0xFFFF);
    if (endpointSetParam((uint16_t)attachEP->addr, USB_CSTRACE_EP_REQ_SET_BUF_SIZE, bufferBlocks) < 0) {
        throw USBException("Failed to set ETR buffer size");
    }
    // To set sink buffer watermark, the unit is 4k blocks. This is controls when the sink will send an
    // interrupt to the driver to trigger data transfer to the host and is specified by the amount of 
    // free space left in the buffer
    uint16_t wmBlocks = (uint16_t)((m_bufferWM / 4096) & 0xFFFF);
    if (endpointSetParam((uint16_t)attachEP->addr, USB_CSTRACE_EP_REQ_SET_WATER_MARK, wmBlocks) < 0) {
        throw USBException("Failed to set ETR buffer size");
    }
    // To attach the sink to the data channel to allow target to send data once collected
    if (endpointSetParam((uint16_t)attachEP->addr, USB_CSTRACE_EP_REQ_ATTACH_STREAM, 0) < 0) {
        throw USBException("Failed to attach ETR");
    }

    // Metadata is checked periodically. This is one of those times.
    submitControlChannelRequest();

    // successfully attached : update state
    attachEP->attachedSink = sink;

    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
    sinkState->transportID = (unsigned)attachEP->addr;
}

//-------------------------------------------------------
// StreamingTraceUSBClient::epForSink
//-------------------------------------------------------
StreamingTraceUSBClient::EndpointInfoSeq::iterator
StreamingTraceUSBClient::epForSink(int sink) {
    int __attribute__((unused)) epAddr = -1;
    for (std::vector<EndpointInfo>::iterator e = m_traceEndpoints.begin();
         e != m_traceEndpoints.end();
         ++e) {
        if (e->attachedSink == sink) { return e; }
    }
    return m_traceEndpoints.end();
}

//-------------------------------------------------------
// StreamingTraceUSBClient::detachDevice
//-------------------------------------------------------
void
StreamingTraceUSBClient::detachDevice(int sink) {

    EndpointInfoSeq::iterator attachedEP = epForSink(sink);

    if (attachedEP != m_traceEndpoints.end()) {
        endpointSetParam((uint16_t)attachedEP->addr, USB_CSTRACE_EP_REQ_DETACH_STREAM, 0);
        // Note that we still continue even if detaching req fails above.
        attachedEP->attachedSink = -1;
    }

    // control channel request will be cancelled by doCancelPendingBuffers()
    // need to complete pending transfers to free resources
    while (m_usb->pendingTransfers() > 0) {
        USBDevice::Transfer_Status status;
        size_t used;
        int token = m_usb->completeTransfer(&status, &used);
        if (token == -1) { break; }
    }
}

//-------------------------------------------------------
// StreamingTraceUSBClient::startDevice
//-------------------------------------------------------
void
StreamingTraceUSBClient::startDevice(int sink) {
    EndpointInfoSeq::iterator ep = epForSink(sink);

    if (ep == m_traceEndpoints.end()) {
        throw USBException("No endpoint attached to sink");
    }
    if (m_controlToken == -1) {
        submitControlChannelRequest();
    }
    if (endpointSetParam((uint16_t)ep->addr, USB_CSTRACE_EP_REQ_START_STREAM, 0) < 0) {
        throw USBException("Failed to start ETR");
    }
}

//-------------------------------------------------------
// StreamingTraceUSBClient::stopDevice
//-------------------------------------------------------
void
StreamingTraceUSBClient::stopDevice(int sink __attribute__((unused)))
{
}

//-------------------------------------------------------
// StreamingTraceUSBClient::doFlush
//-------------------------------------------------------
void
StreamingTraceUSBClient::doFlush(int sink) {
    EndpointInfoSeq::iterator ep = epForSink(sink);

    if (ep == m_traceEndpoints.end()) {
        throw USBException("No endpoint attached to sink");
    }
    // Stop the trace capture on the target
    // this will flush the ETR and initiate the stop sequence
    // when all data is received, CSTREAM_END_SESSION will be sent
    if (endpointSetParam((uint16_t)ep->addr, USB_CSTRACE_EP_REQ_STOP_STREAM, 0) < 0) {
        throw USBException("Failed to stop ETR");
    }
}

//-------------------------------------------------------
// StreamingTraceUSBClient::completeUsbTransfer
//-------------------------------------------------------
bool
StreamingTraceUSBClient::completeUsbTransfer(int sink, int token,
                                             USBDevice::Transfer_Status status,
                                             size_t used) {
    if (token != m_controlToken) {
        //printf("Received token %d instead of expected %d.\n", token, m_controlToken);
        return false;
    }

    if (status == USBDevice::Transfer_SUCCESS
            && used >= sizeof(m_controlMsg)) {

        switch (m_controlMsg.status) {
            case CSTREAM_END_SESSION:
                //printf("Received ctrl message: END SESSION streamIdx=%d\n", m_controlMsg.streamIdx);
                // Ignore the message if sent to the wrong sink
                if (sink == m_controlMsg.streamIdx) {
                    doSendStateEvent(sink, CSTRACE_EVENT_TYPE_END_OF_DATA);
                }
                break;
            case CSTREAM_DETACHED:        // No need to handle this case
                //printf("Received ctrl message: DETACHED streamIdx=%d\n", m_controlMsg.streamIdx);
                break;
            case CSTREAM_BUSY:            // No need to handle this case
                //printf("Received ctrl message: BUSY streamIdx=%d\n", m_controlMsg.streamIdx);
                break;
            case CSTREAM_ATTACHED:        // No need to handle this case
                //printf("Received ctrl message: ATTACHED streamIdx=%d\n", m_controlMsg.streamIdx);
                break;  
            case CSTREAM_DETACHING:       // No need to handle this case
                //printf("Received ctrl message: DETACHING streamIdx=%d\n", m_controlMsg.streamIdx);
                break;
            case CSTREAM_PREPARE_SESSION: // No need to handle this case
                //printf("Received ctrl message: PREPARE SESSION streamIdx=%d\n", m_controlMsg.streamIdx);
                break;
            default:
                //printf("Received UNKNOWN ctrl message %d streamIdx=%d\n", m_controlMsg.status, m_controlMsg.streamIdx);
                break;
        }
        submitControlChannelRequest();
        return true;
    } else if (status == USBDevice::Transfer_NODATA) {
        submitControlChannelRequest();
        return true;
    } else if ((status == USBDevice::Transfer_CANCELLED) && (GetState(sink) == SinkState::DETACHING)) {
        submitControlChannelRequest();
        return true;
    } else {
        // Usually when a control msg is processed, another control message
        // read req is submitted which changes the value of m_controlToken but
        // on erroneous response for a control, it's better to fail than submit
        // another control msg read req.
        //printf("Received ctrl packet status %d of size %ld (exp %ld) and sink %d (exp %d).\n", status, used, sizeof(m_controlMsg), m_controlMsg.streamIdx, sink);
        m_controlToken = -1;
        return false;
    }
}

//-------------------------------------------------------
// StreamingTraceUSBClient::submitControlChannelRequest
//-------------------------------------------------------
void
StreamingTraceUSBClient::submitControlChannelRequest() {
    m_controlToken = m_usb->submitReadTransfer(m_controlEP,
                                               (uint8_t*)&m_controlMsg,
                                               sizeof(m_controlMsg));
}

//-------------------------------------------------------
// StreamingTraceUSBClient::endpointSetParam
//-------------------------------------------------------
int
StreamingTraceUSBClient::endpointSetParam(uint16_t addr, uint8_t request, uint16_t value) {
    return m_usb->controlTransfer(USBDevice::CONTROL_DIR_OUT |
                                  USBDevice::CONTROL_TYPE_VENDOR |
                                  USBDevice::CONTROL_RECIPIENT_ENDPOINT,
                                  request, value, addr,
                                  0, 0, 1000);
}

//-------------------------------------------------------
// StreamingTraceUSBClient::setSinkMetadata
//-------------------------------------------------------
void
StreamingTraceUSBClient::setSinkMetadata(void) {
    // Option way to set ETRname : address map. Unused in current implementation.
}
