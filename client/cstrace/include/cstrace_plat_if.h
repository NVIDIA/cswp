/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSTRACE_PLAT_IF_H
#define CSTRACE_PLAT_IF_H

#include "transport_cfg.h"
#include "cstrace_usb.h"

#include <vector>

//---------------------------------------------------------
// Class StreamingTraceUSBClient
//---------------------------------------------------------
class StreamingTraceUSBClient : public StreamingTraceUSB {
public:
    StreamingTraceUSBClient(transport_cfg_t trasnport_cfg);
    ~StreamingTraceUSBClient();

    void setBufferSize(size_t bufferSize) { m_bufferSize = bufferSize; }
    void setBufferWM(size_t bufferWM) { m_bufferWM = bufferWM; }

    void setSinkMetadata(void);

private:
    // trace stream endpoint addresses
    struct EndpointInfo {
        int addr;
        int attachedSink;
    };
    // trace control message
    struct cs_trace_ctl_msg {
        uint16_t streamIdx;
        uint16_t status;
        uint32_t value;
    };

    typedef std::vector<EndpointInfo> EndpointInfoSeq;

    virtual std::vector<const USBDeviceIdentifier*> getDeviceIDs();
    virtual std::vector<SinkInfo> discoverSinks();
    virtual std::string getTargetIdentifier();
    virtual int getControlToken();
    virtual void usbSetup();
    virtual void usbTeardown();
    virtual void attachDevice(int sink);
    virtual void detachDevice(int sink);
    virtual void startDevice(int sink);
    virtual void stopDevice(int sink);
    virtual void doFlush(int sink);
    virtual bool completeUsbTransfer(int sink, int token, USBDevice::Transfer_Status status, size_t used);

    void submitControlChannelRequest();
    EndpointInfoSeq::iterator epForSink(int sink);
    int endpointSetParam(uint16_t addr, uint8_t request, uint16_t value);

    unsigned                             m_vid;
    unsigned                             m_pid;
    unsigned                             m_speed_id;
    unsigned                             m_interface;
    std::string                          m_targetID;
    std::unique_ptr<USBDeviceIdentifier> m_usbID;
    size_t                               m_bufferSize;
    size_t                               m_bufferWM;
    EndpointInfoSeq                      m_traceEndpoints;
    int                                  m_controlEP;
    int                                  m_controlToken;
    cs_trace_ctl_msg                     m_controlMsg;
    std::map<std::string, std::string>   m_sinkMeta;
};

// Defines for endpoint reqs and sink status

#define USB_CSTRACE_INTF_REQ_STREAM_TMC_INFO 0
// Stream configuration commands
#define USB_CSTRACE_EP_REQ_GET_SINK          0
#define USB_CSTRACE_EP_REQ_SET_SINK          1
#define USB_CSTRACE_EP_REQ_GET_TX_TIMEOUT    2
#define USB_CSTRACE_EP_REQ_SET_TX_TIMEOUT    3
#define USB_CSTRACE_EP_REQ_GET_BUF_SIZE      4
#define USB_CSTRACE_EP_REQ_SET_BUF_SIZE      5
#define USB_CSTRACE_EP_REQ_GET_WATER_MARK    6
#define USB_CSTRACE_EP_REQ_SET_WATER_MARK    7
// Stream session control commands
#define USB_CSTRACE_EP_REQ_ATTACH_STREAM     16
#define USB_CSTRACE_EP_REQ_DETACH_STREAM     17
#define USB_CSTRACE_EP_REQ_START_STREAM	     18
#define USB_CSTRACE_EP_REQ_FLUSH_STREAM      19
#define USB_CSTRACE_EP_REQ_STOP_STREAM       20
// Control message status defines
#define CSTREAM_DETACHED                     0U
#define CSTREAM_BUSY                         1U
#define CSTREAM_DETACHING                    2U
#define CSTREAM_ATTACHED                     3U
#define CSTREAM_PREPARE_SESSION              4U
#define CSTREAM_END_SESSION                  5U

// ETR parameters: 8kB buffer, interrupt when 8kB full
// Warning: as of RASFW v88 this is ignored
#define DEFAULT_ETR_BUFFER_SIZE              (8U * 1024U)
#define DEFAULT_ETR_WATERMARK                (4U * 1024U)
#define ETR_TX_TIMEOUT                       100U
// This is per CSWP spec
#define USB_TRACE_BUFFERS_SIZE               DEFAULT_ETR_BUFFER_SIZE
#define USB_TRACE_BUFFERS_COUNT              11
#define USB_TRACE_EVTBUFFERS_SIZE            256
#define USB_TRACE_EVTBUFFERS_COUNT           1

#endif // CSTRACE_PLAT_IF_H
