/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSTRACE_TYPES_H
#define CSTRACE_TYPES_H

#include <stdint.h>
#include <stddef.h>

//------------------------------------------------------------------------------
// CSTraceEventType
//
// Types of event that can be returned to a client
//------------------------------------------------------------------------------
typedef enum {
    CSTRACE_EVENT_TYPE_NONE,  // pBuf does not contain a valid event
    CSTRACE_EVENT_TYPE_EVENT, // pBuf contains a JSON event notification
    CSTRACE_EVENT_TYPE_ERROR, // pBuf contains a JSON error notification
    CSTRACE_EVENT_TYPE_DATA,  // pBuf contains trace data
    CSTRACE_EVENT_TYPE_END_OF_DATA, // indicates that StreamingTrace_Flush() has completed
    // Values between CUSTOM_S and CUSTOM_E indicate that pBuf contains transport specific data
    CSTRACE_EVENT_TYPE_CUSTOM_S = 0x10000000,
    CSTRACE_EVENT_TYPE_CUSTOM_E = 0x7FFFFFFF,
} CSTraceEventType;

//------------------------------------------------------------------------------
// CSTraceEventBuffer
//
// Buffer to receive events from a trace sink. Events can be trace data,
// information messages, error messages or custom events. Clients should
// allocate the buffer (pBuf) and set size to indicate the size of the
// allocated buffer. When the buffer is returned to the client by
// StreamingTrace_WaitForEvent(), type will be set to indicate the event type
// and used will be set with the number of bytes used in the buffer.
//------------------------------------------------------------------------------
typedef struct {
    CSTraceEventType type; // Event type
    uint8_t* pBuf;        // Client supplied buffer
    size_t size;          // Size of client supplied buffer
    size_t used;          // Bytes used in buffer
} CSTraceEventBuffer;

#endif /* CSTRACE_TYPES_H */
