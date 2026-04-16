/**
 * @file cswp_client_nv_ext_commands.h
 * @brief CSWP client command/response encoding and decoding for NVIDIA extensions commands
 * These commands are specific to NVIDIA chips and are designed for transport security reasons.
 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#ifndef CSWP_CLIENT_NV_EXT_COMMANDS_H
#define CSWP_CLIENT_NV_EXT_COMMANDS_H

#include "cswp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode a CSWP_NVSEC_UNLOCK command
 *
 * @param buf The buffer to encode to
 * @param enable_req extra debug enable signals
 */
int cswp_encode_nvsec_unlock_command(CSWP_BUFFER* buf,
                                     varint_t     enable_req);

/**
 * Encode a CSWP_NVSEC_TOKEN command
 *
 * @param buf The buffer to encode to
 * @param size size of signed token array
 * @param signed_token array that holds signed token
 */
int cswp_encode_nvsec_token_command(CSWP_BUFFER* buf,
                                    size_t       size,
                                    uint8_t*     signed_token);

/**
 * Encode a CSWP_NVSEC_VERIFY command
 *
 * @param buf The buffer to encode to
 * @param challenge challenge string
 */
int cswp_encode_nvsec_verify_command(CSWP_BUFFER* buf,
                                     const char * challenge);

/**
 * Decode a CSWP_NVSEC_VERIFY response
 *
 * @param buf The buffer to decode from
 * @param size size of signature returned
 * @param signature signature returned by server
 * @return Error code: CSWP_SUCCESS on success, or other cswp_result_t on error
 */
int cswp_decode_nvsec_verify_response_body(CSWP_BUFFER* buf,
                                            size_t      size,
                                            char*       signature);

/**
 * Encode a CSWP_NVSEC_ENCRYPT command
 *
 * @param buf The buffer to encode to
 * @param client_public_key client public key
 */
int cswp_encode_nvsec_encrypt_command(CSWP_BUFFER* buf,
                                      const char * client_public_key);



/**
 * Decode a CSWP_NVSEC_ENCRYPT response
 *
 * @param buf The buffer to decode from
 * @param keysize size of server_public_key returned
 * @param server_public_key public key returned by server
 * @return Error code: CSWP_SUCCESS on success, or other cswp_result_t on error
 */
int cswp_decode_nvsec_encrypt_response_body(CSWP_BUFFER* buf,
                                            size_t       keysize,
                                            char *       server_public_key);

#ifdef __cplusplus
}
#endif

#endif /* CSWP_CLIENT_NV_EXT_COMMANDS_H */
