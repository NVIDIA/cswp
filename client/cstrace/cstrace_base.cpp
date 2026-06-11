/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#include "cstrace_base.h"
#include "cstrace_ex.h"

//--------------------------
// StreamingTraceBase
// Constructor, Destructor
//--------------------------
StreamingTraceBase::StreamingTraceBase()
    : m_nextToken(0), m_dataCallback(nullptr), m_dumpPath(nullptr) {}

StreamingTraceBase::~StreamingTraceBase() {
    // Disconnect() must be called from the most-derived class destructor
    // (where virtual methods still work). If it wasn't called, at least
    // prevent std::terminate by joining (thread should already be stopped).
    if (m_dataThread && m_dataThread->joinable()) {
        m_dataThread->join();
    }
}

//-----------------------------
// StreamingTraceBase::Connect
//-----------------------------
void
StreamingTraceBase::Connect() {

    PlatMutex::ScopedLock lock(m_lock);
    if (isConnected()) {
        return;
    }
    doConnect();
    m_sinkInfo = discoverSinks();

    // need at least one event for stop
    for (std::vector<SinkInfo>::iterator sink = m_sinkInfo.begin();
         sink != m_sinkInfo.end();
         ++sink) {
        if (sink->details.eventBufferCount == 0) {
            sink->details.eventBufferCount = 1;
            sink->details.eventBufferSize = 256;
        }
    }
    m_sinkState.resize(m_sinkInfo.size());
    for (std::vector<SinkStatePtr>::iterator sinkState = m_sinkState.begin();
         sinkState != m_sinkState.end();
         ++sinkState) {
        (*sinkState).reset(new SinkState());
    }
}

//--------------------------------
// StreamingTraceBase::Disconnect
//--------------------------------
void
StreamingTraceBase::Disconnect() {
    PlatMutex::ScopedLock lock(m_lock);

    // Stop the data thread before tearing down USB to prevent a
    // use-after-free crash: the data thread may be blocked inside
    // completeTransfer() -> libusb_handle_events_completed() with the
    // lock released.  If doDisconnect() frees the USB handle while
    // the thread is still inside libusb, we get a SIGSEGV.

    // 1. Signal all sinks to detach so the data thread's
    //    WaitForDataEvent loop will exit on its next iteration.
    for (size_t i = 0; i < m_sinkState.size(); ++i) {
        if (m_sinkState[i] &&
            m_sinkState[i]->status != SinkState::DETACHED) {
            m_sinkState[i]->status = SinkState::DETACHED;
            m_sinkState[i]->pendingBuffersCond.notify_all();
        }
    }

    // 2. Cancel in-flight USB transfers so the data thread unblocks
    //    from the blocking completeTransfer() call.
    for (size_t i = 0; i < m_sinkState.size(); ++i) {
        doCancelPendingBuffers((int)i);
    }

    // 3. Wait for the data thread to finish before touching USB state.
    if (m_dataThread && m_dataThread->joinable()) {
        lock.unlock();
        m_dataThread->join();
        lock.lock();
        m_dataThread.reset();
    }

    // 4. Now safe to tear down USB and clear state.
    doDisconnect();
    m_dataCallback = nullptr;
    m_dumpPath = nullptr;
    m_sinkInfo.clear();
    m_sinkState.clear();
}

//---------------------------------
// StreamingTraceBase::GetSinkCount
//---------------------------------
int
StreamingTraceBase::GetSinkCount() {
    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();
    return (int)m_sinkInfo.size();
}

//-----------------------------------
// StreamingTraceBase::GetSinkDetails
//------------------------------------
SinkDetails
StreamingTraceBase::GetSinkDetails(int sink) {
    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();
    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    return m_sinkInfo[(uint64_t)sink].details;
}

//-----------------------------------
// StreamingTraceBase::GetConfigItem
//-----------------------------------
std::string
StreamingTraceBase::GetConfigItem(int sink, const char* name) {
    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();
    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    if (name) {
        std::string key(name);
        if (key == "name") {
            return m_sinkInfo[(uint64_t)sink].details.name;
        } else if (key == "metadata") {
            return m_sinkInfo[(uint64_t)sink].details.metadata;
        }
    }
    return "";
}

//-----------------------------------
// StreamingTraceBase::SetConfigItem
//-----------------------------------
void
StreamingTraceBase::SetConfigItem(int sink, const char* name __attribute__((unused)), const char* value __attribute__((unused))) {
    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();
    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
}

//-----------------------------
// StreamingTraceBase::Attach
//-----------------------------
void
StreamingTraceBase::Attach(int sink) {
    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();
    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }

    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
    if (sinkState->status == SinkState::DETACHED) { sinkState->status = SinkState::ATTACHED; }

    attachDevice(sink);
    m_dataThread = std::make_unique<std::thread>(std::bind(&StreamingTraceBase::dataThread, this, sink));
}

//----------------------------------
// StreamingTraceBase::attachDevice
//----------------------------------
void
StreamingTraceBase::attachDevice(int sink __attribute__((unused))) {
    // no action here - implementations may override for device specific actions
}

//-----------------------------
// StreamingTraceBase::Detach
//-----------------------------
void
StreamingTraceBase::Detach(int sink) {

    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }

    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
    sinkState->status       = SinkState::DETACHED;
    sinkState->pendingBuffersCond.notify_all();

    // wait for data thread to exit
    if (m_dataThread.get()) {
        lock.unlock();
        m_dataThread->join();
        lock.lock();
        m_dataThread.reset(0);
    }

    // cancel in progress transactions
    doCancelPendingBuffers(sink);

    // client won't call WaitForEvent, so wait for and complete any
    // pending buffers now
    while (!sinkState->pendingBuffers.empty()) {
        if (!waitForBuffer(sink, sinkState->pendingBuffers.front()))
            break;
        sinkState->pendingBuffers.pop_front();
    }

    detachDevice(sink);

    sinkState->queuedBuffers.clear();
    sinkState->pendingBuffers.clear();
    sinkState->eventBuffers.clear();
}

//----------------------------------
// StreamingTraceBase::detachDevice
//----------------------------------
void
StreamingTraceBase::detachDevice(int sink __attribute__((unused))) {
    // no action here - implementations may override for device specific actions
}

//----------------------------------------------------------
// StreamingTraceBase::SubmitEventBuffer
//
// Submit a buffer to receive data/events from a trace sink.
// Buffers are not submitted to the lower layers until the
// sink has been started
//----------------------------------------------------------
void
StreamingTraceBase::SubmitEventBuffer(int sink,
                                      int bufferType,
                                      CSTraceEventBuffer* pEventBuffer,
                                      int* pEventToken) {

    PlatMutex::ScopedLock lock(m_lock);

    CheckConnected();

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    if (sinkState->status == SinkState::DETACHED) {
        throw CSTraceEx(CSTRACE_SINK_NOT_ATTACHED, "Sink not attached");
    }

    // reset type and used state so that target can modify it while responding
    // Allocate token to the buffer
    pEventBuffer->type = CSTRACE_EVENT_TYPE_NONE;
    pEventBuffer->used = 0;
    int token          = m_nextToken;
    ++m_nextToken;

    // add buffer to queue
    Buffer buf = { token, pEventBuffer };

    if (bufferType == CSTRACE_EVENT_TYPE_DATA) {
        sinkState->queuedBuffers.push_back(buf);
        // submit to transport layer if started and sufficient space
        if (sinkState->status == SinkState::ACTIVE)
            submitBuffers(sink);
    } else if (bufferType == CSTRACE_EVENT_TYPE_EVENT) {
        sinkState->eventBuffers.push_back(buf);
    } else {
        throw CSTraceEx(CSTRACE_INVALID_EVENT_TYPE, "Invalid buffer type");
    }
    *pEventToken = token;
}

//-------------------------------------------
// StreamingTraceBase::WaitForEvent
//
// Wait for the next buffer from a trace sink
//--------------------------------------------
int
StreamingTraceBase::WaitForEvent(int sink, int msTimeout) {
    PlatMutex::ScopedLock lock(m_lock);

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    // Set deadline from msTimeout provided
    struct timespec start_time, deadline;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    deadline.tv_sec  = start_time.tv_sec + (msTimeout / 1000);
    deadline.tv_nsec = start_time.tv_nsec + ((msTimeout % 1000) * 1000000);
    // Normalize if nanoseconds exceed 1 second
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    // wait for token to be added to completed token queue - either from data
    // thread or client event
    while (sinkState->completedEventTokens.empty()) {
        if (!sinkState->completedEventCond.timed_wait(lock, deadline))
            break;
        if (sinkState->status == SinkState::EXITED) {
            throw CSTraceEx(CSTRACE_COMMSERROR, "Data thread exited due to an error.");
        }
    }

    if (sinkState->completedEventTokens.empty()) {
        throw CSTraceEx(CSTRACE_TIMEOUT, "No data from server in a while...");
    }

    int token = sinkState->completedEventTokens.front();
    sinkState->completedEventTokens.pop();
    return token;
}

//--------------------------------
// StreamingTraceBase::dataThread
//--------------------------------
void
StreamingTraceBase::dataThread(int sink) {
    try
    {
        while (WaitForDataEvent(sink))
            ;
    }
    catch (...)
    {
        // exit thread cleanly
    }
    // Let everyone know we are exiting
    m_lock.lock();
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
    sinkState->status = SinkState::EXITED;
    sinkState->completedEventCond.notify_all();
    m_lock.unlock();
}

//--------------------------------------
// StreamingTraceBase::WaitForDataEvent
//--------------------------------------
bool
StreamingTraceBase::WaitForDataEvent(int sink) {
    PlatMutex::ScopedLock lock(m_lock);

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    // wait for a buffer to be submitted
    while (sinkState->pendingBuffers.empty() &&
           isConnected() &&
           sinkState->status != SinkState::DETACHED) {
        sinkState->pendingBuffersCond.wait(lock);
    }
    if (!isConnected() ||
        sinkState->status == SinkState::DETACHED) {
        return false; // indicate thread exit
    }
    Buffer& buf = sinkState->pendingBuffers.front();

    bool gotBuffer = waitForBuffer(sink, buf);
    if (gotBuffer) {
        int completedToken = buf.token;
        unsigned int used = buf.pEventBuffer->used;
        int type = buf.pEventBuffer->type;
        if (type == CSTRACE_EVENT_TYPE_DATA && used > 0 && m_dataCallback) {
            m_dataCallback(buf.pEventBuffer->pBuf, used, m_dumpPath);
        }
        // remove from pending buffers
        // don't need to track buffer pointer any longer
        sinkState->pendingBuffers.pop_front();
        // submit next buffer
        if (sinkState->status == SinkState::ACTIVE)
            submitBuffers(sink);
        sinkState->completedEventTokens.push(completedToken);
        sinkState->completedEventCond.notify_one();
    }

    return gotBuffer;
}

//-----------------------------------------------------------
// StreamingTraceBase::Start
//
// Start data collection from a sink. Any buffers previously
// submitted are passed to the lower layers.
//-----------------------------------------------------------
void
StreamingTraceBase::Start(int sink) {
    PlatMutex::ScopedLock lock(m_lock);

    CheckConnected();

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    if (sinkState->status == SinkState::DETACHED) {
        throw CSTraceEx(CSTRACE_SINK_NOT_ATTACHED, "Sink not attached");
    }
    sinkState->status = SinkState::ACTIVE;

    // submit buffers to transport layer
    submitBuffers(sink);
    startDevice(sink);
}

//----------------------------------
// StreamingTraceBase::startDevice
//----------------------------------
void
StreamingTraceBase::startDevice(int sink __attribute__((unused))) {
    // no action here - implementations may override for device specific actions
}

//----------------------------------
// StreamingTraceBase::Stop
//
// Stop data collection from a sink
//----------------------------------
void
StreamingTraceBase::Stop(int sink) {
    PlatMutex::ScopedLock lock(m_lock);

    CheckConnected();

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    if (sinkState->status == SinkState::DETACHING) {
        return;
    }
    if (sinkState->status != SinkState::ACTIVE) {
        throw CSTraceEx(CSTRACE_SINK_NOT_STARTED, "Sink not started");
    }
    stopDevice(sink);

    sinkState->status = SinkState::DETACHING;

    // cancel in progress transactions
    doCancelPendingBuffers(sink);

    // complete event buffers
    while (!sinkState->eventBuffers.empty()) {
        Buffer& buf = sinkState->eventBuffers.front();
        buf.pEventBuffer->type = CSTRACE_EVENT_TYPE_NONE;
        buf.pEventBuffer->used = 0;
        int token = buf.token;
        sinkState->eventBuffers.pop_front();
        sinkState->completedEventTokens.push(token);
    }

    if (!sinkState->completedEventTokens.empty()) {
        sinkState->completedEventCond.notify_one();
    }
    // buffers will be returned to client via WaitForEvent()
}

//---------------------------------
// StreamingTraceBase::stopDevice
//---------------------------------
void
StreamingTraceBase::stopDevice(int sink __attribute__((unused))) {
    // no action here - implementations may override for device specific actions
}

//--------------------------------------------------------------
// StreamingTraceBase::Flush
//
// Implementations should return control of any pending
// buffers to the client via WaitForEvent which may submit more
// buffers.
//--------------------------------------------------------------
void
StreamingTraceBase::Flush(int sink) {
    PlatMutex::ScopedLock lock(m_lock);
    CheckConnected();

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    if (sinkState->status == SinkState::DETACHED) {
        throw CSTraceEx(CSTRACE_SINK_NOT_ATTACHED, "Sink not attached");
    }
    doFlush(sink);
}

//----------------------------------------------
// StreamingTraceBase::SendStateEvent
//
// Add an event directly to the complete queue
//----------------------------------------------
void
StreamingTraceBase::SendStateEvent(int sink, CSTraceEventType eventType) {
    PlatMutex::ScopedLock lock(m_lock);

    if (sink >= (int)m_sinkInfo.size()) {
        throw CSTraceEx(CSTRACE_INVALID_SINK, "Invalid sink");
    }
    doSendStateEvent(sink, eventType);
}

//--------------------------------------
// StreamingTraceBase::doSendStateEvent
//-------------------------------------
void
StreamingTraceBase::doSendStateEvent(int sink, CSTraceEventType eventType) {
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];

    // complete event buffer if available
    if (!sinkState->eventBuffers.empty())
    {
        Buffer& buf = sinkState->eventBuffers.front();
        buf.pEventBuffer->type = eventType;
        int token = buf.token;
        sinkState->eventBuffers.pop_front();
        sinkState->completedEventTokens.push(token);
        sinkState->completedEventCond.notify_one();
    }
}

//------------------------------------
// StreamingTraceBase::CheckConnected
//
// called with lock held
//------------------------------------
void
StreamingTraceBase::CheckConnected() {
    if (!isConnected()) {
        throw CSTraceEx(CSTRACE_NO_CONNECTION, "Streaming trace is not connected");
    }
}

//-------------------------------------------------------------------
// StreamingTraceBase::submitBuffers
//
// submit buffers to implementation. The implementation indicates how
// many pending buffers it can handle with SinkInfo.bufferCount.
// Called with lock held.
//--------------------------------------------------------------------
void
StreamingTraceBase::submitBuffers(int sink) {
    SinkStatePtr& sinkState = m_sinkState[(uint64_t)sink];
    SinkInfo& sinkInfo = m_sinkInfo[(uint64_t)sink];
    while (sinkState->pendingBuffers.size() < sinkInfo.details.dataBufferCount &&
           !sinkState->queuedBuffers.empty()) {
        Buffer buf = sinkState->queuedBuffers.front();
        submitBuffer(sink, buf);

        // move to pending queue
        sinkState->queuedBuffers.pop_front();
        sinkState->pendingBuffers.push_back(buf);
        sinkState->pendingBuffersCond.notify_one();
    }
}

//-------------------------------------------------------------------
// StreamingTraceBase::SetTraceDataCallback
//
// Set the callback function to dump trace data to a file.
// This is needed for NVRDDI as ARMDS drives the capture loop and
// cannot use the client API to dump trace data.
//-------------------------------------------------------------------
void
StreamingTraceBase::SetTraceDataCallback(TraceDataCallback cb, FILE* dump_path) {
    PlatMutex::ScopedLock lock(m_lock);
    m_dataCallback = cb;
    m_dumpPath = dump_path;
}

//-------------------------------------------------------------------
// StreamingTraceBase::ClearTraceDataCallback
//
// Clear the callback function to dump trace data to a file.
// This is needed for NVRDDI as ARMDS drives the capture loop and
// cannot use the client API to dump trace data.
//-------------------------------------------------------------------
void
StreamingTraceBase::ClearTraceDataCallback() {
    PlatMutex::ScopedLock lock(m_lock);
    m_dataCallback = nullptr;
    m_dumpPath = nullptr;
}