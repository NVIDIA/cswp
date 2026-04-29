/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#include "cswp_client_common.h"
#include "cswp_client_nv_ext_commands.h"
#include "cswp_buffer.h"

int cswp_encode_nvsec_unlock_command(CSWP_BUFFER* buf,
                                     varint_t enable_req)
{
    int res = CSWP_SUCCESS;
    __CSWP_CHECK(cswp_encode_command_header(buf, CSWP_NVSEC_UNLOCK));
    __CSWP_CHECK(cswp_buffer_put_varint(buf, enable_req));
    return res;
}

int cswp_encode_nvsec_token_command(CSWP_BUFFER* buf,
                                    size_t       size,
                                    uint8_t*     signed_token)
{
    int res = CSWP_SUCCESS;
    __CSWP_CHECK(cswp_encode_command_header(buf, CSWP_NVSEC_TOKEN));
    __CSWP_CHECK(cswp_buffer_put_data(buf, signed_token, size));
    return res;
}

int cswp_encode_nvsec_verify_command(CSWP_BUFFER* buf,
                                     const char* challenge)
{
    int res = CSWP_SUCCESS;
    __CSWP_CHECK(cswp_encode_command_header(buf, CSWP_NVSEC_VERIFY));
    __CSWP_CHECK(cswp_buffer_put_string(buf, challenge));
    return res;
}

int cswp_decode_nvsec_verify_response_body(CSWP_BUFFER* buf,
                                           size_t       size,
                                           char*        signature)
{
    int res = CSWP_SUCCESS;
    __CSWP_CHECK(cswp_buffer_get_string(buf, signature, size));
    return res;
}

int cswp_encode_nvsec_encrypt_command(CSWP_BUFFER* buf,
                                      const char*  client_public_key)
{
    int res = CSWP_SUCCESS;
    __CSWP_CHECK(cswp_encode_command_header(buf, CSWP_NVSEC_ENCRYPT));
    __CSWP_CHECK(cswp_buffer_put_string(buf, client_public_key));
    return res;
}

int cswp_decode_nvsec_encrypt_response_body(CSWP_BUFFER* buf,
                                            size_t       keysize,
                                            char*        server_public_key)
{
    int res = CSWP_SUCCESS;
    __CSWP_CHECK(cswp_buffer_get_string(buf, server_public_key, keysize));
    return res;
}

/* end of file cswp_client_nv_ext_commands.c */
