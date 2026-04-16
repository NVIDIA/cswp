/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#include "cswp_types.h"

#define __CSWP_CHECK(x) if ((res = (x)) != CSWP_SUCCESS) return res;

/**
 * Encode a command header
 *
 * @param buf The buffer to encode to
 * @param messageType The message type identifier (See cswp_commands_t)
 */
int cswp_encode_command_header(CSWP_BUFFER* buf,
                               varint_t messageType);

/**
 * Decode a response header
 *
 * @param buf The buffer to decode from
 * @param messageType Receives the message type identifier (See cswp_commands_t)
 * @param errorCode Receives the error code (See cswp_result_t)
 */
int cswp_decode_response_header(CSWP_BUFFER* buf,
                                varint_t* messageType,
                                varint_t* errorCode);

/**
 * Decode an error response
 *
 * @param buf The buffer to decode from
 * @param errorMessage Buffer to receives the error message
 * @param errorMessageSize Size of the error message buffer
 */
int cswp_decode_error_response_body(CSWP_BUFFER* buf,
                                    char* errorMessage,
                                    size_t errorMessageSize);
