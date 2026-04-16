/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSWP_CLIENT_EX_H
#define CSWP_CLIENT_EX_H

#include <stdexcept>
#include <string>

//-----------------------------------------------------------
// class CDebugEx
//
// Throw one of these to wrap an error
//-----------------------------------------------------------
class CDebugEx : public std::exception {
public:
    explicit CDebugEx(unsigned code, const char* detail) :  m_code(code), m_text(detail) {}
    explicit CDebugEx(unsigned code, const std::string& detail) :  m_code(code), m_text(detail) {}

    virtual const char* what() const throw() { return m_text.c_str(); }
    unsigned code() const throw() { return m_code; }
    ~CDebugEx() throw() {}

private:
    unsigned    m_code;
    std::string m_text;
};

/* CSWP configuration errors */
#define CDEBUG_INVALID_TARGET      0x0711
#define CDEBUG_INVALID_CONFIG      0x0712

#endif /* CSWP_CLIENT_EX_H */

