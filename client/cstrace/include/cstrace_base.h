/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSTRACE_BASE_H
#define CSTRACE_BASE_H

#include "cstrace_types.h"
#include "plat_sync_prim.h"

#include <string>
#include <map>
#include <memory>
#include <deque>
#include <queue>
#include <vector>
#include <cstdio>

// NVRDDI lib uses this callback to dump trace data to a file.
typedef void (*TraceDataCallback)(const void* buf, size_t len, FILE* dump_file);

//------------------------------------------------------------------------------
// SinkDetails
//
// Trace sink details
//------------------------------------------------------------------------------
struct SinkDetails {
    std::string name;
    std::string metadata;
    size_t dataBufferCount;
    size_t dataBufferSize;
    size_t eventBufferCount;
    size_t eventBufferSize;
};
#define MAX_NUM_ETRS  32U // Two sockets have 16 ETRs each

//----------------------------------------------------------------
// class StreamingTraceBase
//
// Base class for streaming trace implementations
// Provides the client API functions, managing the discovery
// and description of trace sinks, handling of event buffers etc.
//----------------------------------------------------------------
class StreamingTraceBase {
public:
    StreamingTraceBase();
    virtual ~StreamingTraceBase();

    void Connect();
    void Disconnect();

    void SetTraceDataCallback(TraceDataCallback cb, FILE* dump_path);
    void ClearTraceDataCallback();

    int         GetSinkCount();
    SinkDetails GetSinkDetails(int sink);
    std::string GetConfigItem(int sink, const char* name);
    void        SetConfigItem(int sink, const char* name, const char* value);

    void Attach(int sink);
    void Detach(int sink);
    void Start (int sink);
    void Stop  (int sink);
    void Flush (int sink);

    void SubmitEventBuffer (int sink,  int bufferType,
                            CSTraceEventBuffer* pEventBuffer, int* pEventToken);
    int  WaitForEvent      (int sink, int msTimeout);
    void SendStateEvent    (int sink, CSTraceEventType eventType);

protected:
    // Struct that associates a token to a client supplied buffer
    struct Buffer {
        int token;
        CSTraceEventBuffer* pEventBuffer;
    };
    typedef std::deque<Buffer> BufferQueue;

    // Struct Sink information
    struct SinkInfo {
        SinkDetails details;
    };

    // Struct to hold run time state of a sink
    struct SinkState {
        enum Status {
            DETACHED,
            ATTACHED,
            ACTIVE,
            DETACHING,
            EXITED
        };
        SinkState() : status(DETACHED) { }
        Status                    status;
        unsigned int              transportID;    // identifier for transport layer
        BufferQueue               pendingBuffers; // buffers submitted to transport layer
        BufferQueue               queuedBuffers;  // data buffers not yet submitted to transport layer
        BufferQueue               eventBuffers;   // event buffers for status events. Client posts the events in this buffers based on metadata information received.
        PlatThreadCondVar         pendingBuffersCond;
        std::queue<int>           completedEventTokens;
        PlatThreadCondVar         completedEventCond;
    };
    typedef std::shared_ptr<SinkState> SinkStatePtr;

    // Functions required in implementations
    virtual std::vector<SinkInfo> discoverSinks() = 0;
    virtual void doConnect() = 0;
    virtual void doDisconnect() = 0;
    virtual bool isConnected() = 0;
    virtual void attachDevice(int sink);
    virtual void detachDevice(int sink);
    virtual void submitBuffer(int sink, Buffer& buf) = 0;
    virtual void doCancelPendingBuffers(int sink) = 0;
    virtual void startDevice(int sink);
    virtual void stopDevice(int sink);
    virtual void doFlush(int sink) = 0;
    virtual bool waitForBuffer(int sink, Buffer& buf) = 0;


    void CheckConnected();
    void submitBuffers(int sink);
    void dataThread(int sink);
    bool WaitForDataEvent(int sink);
    void doSendStateEvent(int sink, CSTraceEventType eventType);
    SinkState::Status GetState(int sink) {
        if (sink >= (int)m_sinkInfo.size()) {
            return SinkState::EXITED;
        }
        SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
        return sinkState->status;
    }

    PlatMutex                      m_lock;
    std::vector<SinkInfo>          m_sinkInfo;
    std::vector<SinkStatePtr>      m_sinkState;
    int                            m_nextToken;
    std::unique_ptr<std::thread>   m_dataThread; // consider changing to shared pointer
    TraceDataCallback              m_dataCallback;
    FILE*                          m_dumpPath;
};

#endif // CSTRACE_BASE_H
