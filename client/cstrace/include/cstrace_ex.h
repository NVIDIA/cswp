/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#ifndef CSTRACE_EX_H
#define CSTRACE_EX_H

#include <stdexcept>

//-----------------------------------------------------------
// class CSTraceEx
//
// Throw one of these to wrap an error
//-----------------------------------------------------------
class CSTraceEx : public std::exception {
public:
    explicit CSTraceEx(unsigned code, const char* detail) :  m_code(code), m_text(detail) {}
    explicit CSTraceEx(unsigned code, const std::string& detail) :  m_code(code), m_text(detail) {}

    virtual const char* what() const throw() { return m_text.c_str(); }
    unsigned code() const throw() { return m_code; }
    ~CSTraceEx() throw() {}

private:
    unsigned    m_code;
    std::string m_text;
};

#ifndef DOXYGEN_SHOULD_SKIP_THIS
/* 0x02XX - streaming trace errors */
#define CSTRACE_NO_CONNECTION      0x0201
#define CSTRACE_NO_EVENT           0x0202
#define CSTRACE_COMMSERROR         0x0203
#define CSTRACE_TIMEOUT            0x0204
#define CSTRACE_NO_DEVICE          0x0205
#define CSTRACE_INVALID_SINK       0x0206
#define CSTRACE_SINK_NOT_ATTACHED  0x0207
#define CSTRACE_SINK_NOT_STARTED   0x0208
#define CSTRACE_CONNECT_FAILED     0x0209
#define CSTRACE_SINK_START_FAILED  0x020A
#define CSTRACE_SINK_STOP_FAILED   0x020B
#define CSTRACE_DATA_OVERFLOW      0x020C
#define CSTRACE_DATA_INTEGRITY     0x020D
#define CSTRACE_AUX_PROBE_PROG     0x020E
#define CSTRACE_SET_MODE_FAILED    0x020F
#define CSTRACE_INVALID_EVENT_TYPE 0x0210
#define CSTRACE_INVALID_TARGET     0x0211
#define CSTRACE_INVALID_CONFIG     0x0212

#endif

#endif // CSTRACE_EX_H
