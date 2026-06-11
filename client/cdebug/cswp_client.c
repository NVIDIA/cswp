/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#include "cswp_client.h"
#include "cswp_client_common.h"
#include "cswp_client_commands.h"
#include "cswp_client_nv_ext_commands.h"
#include "cswp_buffer.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

//-----------------------------------------------------------------------------
// Defines
//-----------------------------------------------------------------------------
#define CSWP_ERR_MSG_SIZE 1024
#define CSWP_REQ_HEADER_SIZE (                       \
    4 +  /* message_length (command+header) bytes */ \
    10 + /* num_sub_reqs bytes (allow 10 bytes) */   \
    1)   /* error mode bytes */

//-----------------------------------------------------------------------------
// Client private and response processing related data structures and
// complete function
//-----------------------------------------------------------------------------
typedef int (*complete_func)(cswp_client_t* client, void* replyData);

typedef struct _pending_response_t {
    cswp_commands_t type;             // Expected response message type
    complete_func complete;           // Function to call to process response   
    void* replyData;                  // Argument to pass to completion response
    struct _pending_response_t* next; // Pointer to next response
} pending_response_t;

// Batch mode and whether to continue / abort on error
typedef enum {
    BATCH_NONE,
    BATCH_CONTINUE,
    BATCH_ABORT
} batch_mode_t;

// Private data for CSWP client
typedef struct _cswp_client_priv_t {
    CSWP_BUFFER* hdr;                      // Header buffers
    CSWP_BUFFER* cmd;                      // Request buffer
    CSWP_BUFFER* rsp;                      // Response buffer
    batch_mode_t batch_mode;               // Batch mode
    int num_cmds;                          // Number of command in batch request
    pending_response_t* pending_responses; // Expected response sequence
    int connected;                        // Are we already connected?
    uint64_t cswp_init_serverProtocolVersion; // Server protocol version
    uint64_t cswp_init_serverVersion;      // Server version
    char *cswp_init_serverID;              // Server string version
} cswp_client_priv_t;

//-----------------------------------------------------------------------------
// Server response related data structures
//-----------------------------------------------------------------------------

// cswp_init_rsp_data
struct cswp_init_rsp_data {
    uint64_t* serverProtocolVersion;
    char*     serverID;
    size_t    serverIDSize;
    unsigned* serverVersion;
};
// cswp_get_devices_rsp_data
struct cswp_get_devices_rsp_data {
    unsigned* deviceCount;
    char** deviceList;
    size_t deviceListSize;
    size_t deviceListEntrySize;
    char** deviceTypes;
    size_t deviceTypeSize;
    size_t deviceTypeEntrySize;
};

// cswp_get_system_description_rsp_data
struct cswp_get_system_description_rsp_data {
    unsigned* descriptionFormat;
    unsigned* descriptionSize; 
    uint8_t*  descriptionDataBuffer;
    size_t    bufferSize;
};

// cswp_device_open_rsp_data
struct cswp_device_open_rsp_data {
    char* deviceInfo;      // Buffer for deviceInfo?
    size_t deviceInfoSize; // Size of deviceInfo buffer?
};

// cswp_get_device_capabilities_rsp_data
struct cswp_get_device_capabilities_rsp_data {
    unsigned* capabilities;
    unsigned* capabilityData;
};

// cswp_reg_read_rsp_data
struct cswp_reg_read_rsp_data {
    uint32_t* registerValues;  // Buffer for register value
    size_t registerValuesSize; // Size of registerValues buffer
};

// cswp_reg_list_rsp_data
struct cswp_reg_list_rsp_data {
    unsigned*             registerCount;    // Number of registers
    cswp_register_info_t* registerInfo;     // Register info buffer
    size_t                registerInfoSize; // Size of the registerInfo buffer
    char *strBuf;                           // String buffe
    size_t strBufSize;                      // Size of buffer
};

// cswp_mem_poll_rsp_data
struct cswp_mem_poll_rsp_data {
    uint8_t* buf;      // Buffer for read data
    size_t* bytesRead; // Size of the provided buffer?
};

// cswp_mem_read_rsp_data
struct cswp_mem_read_rsp_data {
    uint8_t* buf;      // Buffer for read data
    size_t* bytesRead; // Number of byte in the provided buffer
};

// cswp_get_config_rsp_data
struct cswp_get_config_rsp_data {
    char*  value;     // Buffer for configuration item value
    size_t valueSize; // Size of value buffer
};

// cswp_nvsec_unlock_rsp_data
struct cswp_nvsec_unlock_rsp_data {
    uint8_t* token_req;           // Buffer for challenge token
    size_t   num_token_req_bytes; // Number of challenge token bytes
};

// cswp_nvsec_verify_rsp_data
struct cswp_nvsec_verify_rsp_data {
    char*  signature;     // Buffer for signature
    size_t signatureSize; // Number of signature bytes
};

// cswp_nvsec_encrypt_rsp_data
struct cswp_nvsec_encrypt_rsp_data {
    char*   server_public_key; // Buffer for publickey received from server
    size_t  key_size;          // Number of server_public_key bytes
};

//-----------------------------------------------------------------------------
// cswp_client_add_node_for_pending_rsp
//-----------------------------------------------------------------------------
static void
cswp_client_add_node_for_pending_rsp(cswp_client_t* client,
                                     cswp_commands_t type,
                                     complete_func complete,
                                     void* rsp_node)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    pending_response_t** pPending;
    pending_response_t* pending_response;

    // go to end of list
    pPending = &priv->pending_responses;
    while (*pPending != NULL) { pPending = &((*pPending)->next); }

    // add to end of list
    pending_response = (pending_response_t*)calloc(sizeof(pending_response_t), 1);
    pending_response->type = type;
    pending_response->complete = complete;
    pending_response->replyData = rsp_node;
    *pPending = pending_response;

    ++priv->num_cmds;
}

//-----------------------------------------------------------------------------
// cswp_client_init
//
// Initialise client buffers before sending CSWP_INIT
//-----------------------------------------------------------------------------
int
cswp_client_init(cswp_client_t* client) 
{
    cswp_client_priv_t* priv;

    client->errorMsg = calloc(1, CSWP_ERR_MSG_SIZE);

    /// Allocate and initialise private data
    priv = calloc(sizeof(cswp_client_priv_t), 1);
    priv->hdr = cswp_buffer_alloc(CSWP_REQ_HEADER_SIZE);
    priv->cmd = cswp_buffer_alloc(CSWP_REQ_RSP_BUFFER_SIZE);
    priv->rsp = cswp_buffer_alloc(CSWP_REQ_RSP_BUFFER_SIZE);
    priv->batch_mode = BATCH_NONE;
    priv->connected = 0;
    priv->cswp_init_serverID = NULL;
    client->priv = priv;

    return CSWP_SUCCESS;
}

//-----------------------------------------------------------------------------
// cswp_client_term
//
// Cleanup client before terminating using CSWP_TERM
//-----------------------------------------------------------------------------
int
cswp_client_term(cswp_client_t* client)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    free(client->errorMsg);
    client->errorMsg = NULL;

    // Cleanup private data
    if (priv) {
        cswp_buffer_free(priv->hdr);
        cswp_buffer_free(priv->cmd);
        cswp_buffer_free(priv->rsp);
        free(client->priv);
        client->priv = NULL;
    }
    return CSWP_SUCCESS;
}

//-----------------------------------------------------------------------------
// cswp_client_prep_priv_cmd_data
//
// Prepare the request buffer to write command data
//-----------------------------------------------------------------------------
static int
cswp_client_prep_priv_cmd_data(cswp_client_t* client)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    if (priv->batch_mode == BATCH_NONE) {
        // Reset buffer, reserving space for message header
        priv->cmd->pos = CSWP_REQ_HEADER_SIZE;
        priv->cmd->used = CSWP_REQ_HEADER_SIZE;
        priv->pending_responses = NULL;
        priv->num_cmds = 0;
    }
    return CSWP_SUCCESS;
}

//-----------------------------------------------------------------------------
// cswp_client_verify_response 
//
// Process received response from the server
//-----------------------------------------------------------------------------
static int
cswp_client_verify_response(cswp_client_t* client,
                             pending_response_t* pendingRsp)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    varint_t msgType, errCode;
    int res = cswp_decode_response_header(priv->rsp, &msgType, &errCode);

    if (res == CSWP_SUCCESS && msgType != pendingRsp->type) {
        res = cswp_client_error(client, CSWP_UNEXPECTED_RESPONSE, "Unexpected response: 0x%lX", msgType);
    }

    if (res == CSWP_SUCCESS && errCode != CSWP_SUCCESS) {
        res = (int)errCode;
        cswp_decode_error_response_body(priv->rsp, client->errorMsg, CSWP_ERR_MSG_SIZE);
    }

    if (res == CSWP_SUCCESS) {
        if (pendingRsp->complete)
            res = pendingRsp->complete(client, pendingRsp->replyData);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_client_transact_send 
//-----------------------------------------------------------------------------
static int
cswp_client_transact_send(cswp_client_t* client)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    // Add num_cmds and batch_mode/err_code to request header space
    // in header buffer
    cswp_buffer_clear(priv->hdr);
    cswp_buffer_put_varint(priv->hdr, (varint_t)priv->num_cmds);
    cswp_buffer_put_uint8(priv->hdr, (uint8_t)priv->batch_mode);

    // Insert header before message body. Note that header may not always
    // need CSWP_REQ_HEADER_SIZE, reqOffset make sure that header attaches
    // to the command itself so that we can flush out the whole request as
    // continuous bytes.
    size_t reqOffset = CSWP_REQ_HEADER_SIZE - 4 /* num message lenth bytes */ - priv->hdr->used;
    uint32_t reqSize = (uint32_t)(priv->cmd->used - reqOffset); // Message length including header
    uint8_t* pBuf = priv->cmd->buf + reqOffset;                 // Pointer to reeOffest byte

    // Add message length to request header space in command buffer
    uint8_t* pHdr = pBuf;
    *pHdr++ = (uint8_t)(reqSize & 0xFF);
    *pHdr++ = (uint8_t)((reqSize >> 8) & 0xFF);
    *pHdr++ = (uint8_t)((reqSize >> 16) & 0xFF);
    *pHdr++ = (uint8_t)((reqSize >> 24) & 0xFF);
    
    // Copy part of header in header buffer to command buffer
    memcpy(pHdr, priv->hdr->buf, priv->hdr->used);

    // Send command to server
    int res = CSWP_SUCCESS;
    res = client->send(client, pBuf, reqSize);

    return res;
}

//-----------------------------------------------------------------------------
// cswp_client_transact_receive
//-----------------------------------------------------------------------------
static int
cswp_client_transact_receive(cswp_client_t* client, unsigned* opsCompleted)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    if (opsCompleted) { *opsCompleted = 0; }

    // Submit a read request to the interface
    int res = CSWP_SUCCESS;
    res = client->receive(client, priv->rsp->buf, priv->rsp->size, &priv->rsp->used);

    // Start processing response received
    // Check header
    if (res == CSWP_SUCCESS) {
        uint32_t rspSize;
        cswp_buffer_seek(priv->rsp, 0); // Reset the buff read pointer to start
        // Check response size aka message length mataches the data received.
        cswp_buffer_get_uint32(priv->rsp, &rspSize);
        if (rspSize > priv->rsp->used)
            res = cswp_client_error(client, CSWP_RESPONSE_SIZE_ERR,
                                    "Incomplete response received.  Received %d bytes, expected %d",
                                    priv->rsp->used, rspSize);
    }

    // Check that all responses are received
    if (res == CSWP_SUCCESS) {
        varint_t numRsps;
        cswp_buffer_get_varint(priv->rsp, &numRsps);
        // Check that all responses are received for all non-async responses
        if (numRsps != (varint_t)priv->num_cmds)
            res = cswp_client_error(client, CSWP_RESPONSE_SIZE_ERR,
                                    "Incomplete response received.  Received %d responses, expected %d",
                                    numRsps, priv->num_cmds);
    }

    if (res == CSWP_SUCCESS) {
        // decode and verify each response
        pending_response_t* pendingRsp;
        pendingRsp = priv->pending_responses;
        while (pendingRsp != NULL && res == CSWP_SUCCESS) {
            res = cswp_client_verify_response(client, pendingRsp);
            if (opsCompleted && res == CSWP_SUCCESS) {(*opsCompleted)++;}
                pendingRsp = pendingRsp->next;
        }
    }

    return res;
}

//-----------------------------------------------------------------------------
// cswp_client_transact 
//
// Send request and receive response
//-----------------------------------------------------------------------------
static int
cswp_client_transact(cswp_client_t* client, unsigned* opsCompleted)
{
    // Transaction
    int res = cswp_client_transact_send(client);
    if (res == CSWP_SUCCESS) {
        res = cswp_client_transact_receive(client, opsCompleted);
    }

    // cleanup response list irrespective of completition status
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    pending_response_t* pendingRsp = priv->pending_responses;
    while (pendingRsp != NULL) {
        pending_response_t* d = pendingRsp;
        if (pendingRsp->replyData) { free(pendingRsp->replyData); }
        pendingRsp = pendingRsp->next;
        free(d);
    }
    priv->pending_responses = NULL;

    return res;
}

//-----------------------------------------------------------------------------
// cswp_client_send_to_transact
//
// In order to support batch messages, command handling is split into two
// parts - the request is encoded into the command buffer and a callback can
// be registered to handle the response when it is received. After encoding the
// command, cswp_client_send_to_transact() is called - when not in batch mode
// the command will be executed immediately, but in batch mode the command will
// not be executed until cswp_batch_end() is called. After each block of
// commands is executed, the list of pending responses is processed.
//-----------------------------------------------------------------------------
static int
cswp_client_send_to_transact(cswp_client_t* client)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    int res = CSWP_SUCCESS;

    if (priv->batch_mode == BATCH_NONE && priv->num_cmds > 0) {
        res = cswp_client_transact(client, NULL);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_client_error
//
// Write an error message into client error message buffer
//-----------------------------------------------------------------------------
int
cswp_client_error(cswp_client_t* client, int errorCode, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(client->errorMsg, CSWP_ERR_MSG_SIZE, fmt, args);
    va_end(args);
    return errorCode;
}

//-----------------------------------------------------------------------------
// cswp_init_complete
//-----------------------------------------------------------------------------
static int
cswp_init_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_init_rsp_data* rsp_node = (struct cswp_init_rsp_data*)replyData;

    varint_t protoVer, svrVer;
    int res = cswp_decode_init_response_body(priv->rsp, &protoVer,
                                             rsp_node->serverID,
                                             rsp_node->serverIDSize, &svrVer);

    if (res == CSWP_SUCCESS) {
        if (rsp_node->serverProtocolVersion)
            *rsp_node->serverProtocolVersion = protoVer;
        if (rsp_node->serverVersion)
            *rsp_node->serverVersion = (unsigned)svrVer;
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_init
//-----------------------------------------------------------------------------
int
cswp_init(cswp_client_t* client,
          const char*    clientID,
          uint64_t       ProtocolVersion,
          uint64_t*      serverProtocolVersion,
          char*          serverID,
          size_t         serverIDSize,
          unsigned*      serverVersion)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    int res = CSWP_SUCCESS;

    /* cswp_init was called before, we allow that but do not send the CSWP
       server an init request again, instead we keep track to only call
       CSWP term once all handles have been released. */
    if (priv->connected > 0) {
        *serverProtocolVersion = priv->cswp_init_serverProtocolVersion;
        strncpy(serverID, priv->cswp_init_serverID, serverIDSize-1);
        serverID[serverIDSize-1] = '\0';
        *serverVersion = priv->cswp_init_serverVersion;
        priv->connected++;
        return CSWP_SUCCESS;
    }

    if (client->connect) {
        res = client->connect(client);
    }

    if (res == CSWP_SUCCESS) {
        cswp_client_prep_priv_cmd_data(client);
        res = cswp_encode_init_command(priv->cmd, ProtocolVersion, clientID);
    } else {
        return res;
    }

    if (res == CSWP_SUCCESS) {
        struct cswp_init_rsp_data* rsp_node = calloc(1, sizeof(struct cswp_init_rsp_data));
        rsp_node->serverProtocolVersion = serverProtocolVersion;
        rsp_node->serverID = serverID;
        rsp_node->serverIDSize = serverIDSize;
        rsp_node->serverVersion = serverVersion;
        cswp_client_add_node_for_pending_rsp(client, CSWP_INIT, cswp_init_complete, rsp_node);
        res = cswp_client_send_to_transact(client);
        if (res == CSWP_SUCCESS) {
            // Save command response data in case cswp_init is called again
            priv->cswp_init_serverProtocolVersion = *serverProtocolVersion;
            priv->cswp_init_serverID = (char *) malloc(strlen(serverID)+1);
            strcpy(priv->cswp_init_serverID, serverID);
            priv->cswp_init_serverVersion = *serverVersion;
            priv->connected = 1;
        }
    }

    // Cleanup if we found an error
    if ((res != CSWP_SUCCESS) && client->disconnect) {
        client->disconnect(client);
    }

    return res;
}

//-----------------------------------------------------------------------------
// cswp_term
//-----------------------------------------------------------------------------
int
cswp_term(cswp_client_t* client)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res;

    if (priv->connected == 0) {
        // We never connected before, we connect here. This path is used
        // when init failed and we suspect it is because there is an
        // active CSWP connection.
        if (client->connect) {
            res = client->connect(client);
            if (res != CSWP_SUCCESS) {
                return res;
            }
        }
    } else if (--priv->connected > 0) {
        return CSWP_SUCCESS;
    }
    
    res = cswp_encode_term_command(priv->cmd);

    if (res == CSWP_SUCCESS) {
        cswp_client_add_node_for_pending_rsp(client, CSWP_TERM, NULL, 0);
        res = cswp_client_send_to_transact(client);
    }

    if (client->disconnect) {
        client->disconnect(client);
    }

    if (priv->cswp_init_serverID != NULL) {
        free(priv->cswp_init_serverID);
        priv->cswp_init_serverID = NULL;
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_batch_begin
//-----------------------------------------------------------------------------
int
cswp_batch_begin(cswp_client_t* client, int abortOnError)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);

    if (abortOnError) { priv->batch_mode = BATCH_ABORT; }
    else              { priv->batch_mode = BATCH_CONTINUE; }

    return CSWP_SUCCESS;
}

//-----------------------------------------------------------------------------
// cwp_batch_end
//-----------------------------------------------------------------------------
int
cswp_batch_end(cswp_client_t* client, unsigned *opsCompleted)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    int res = CSWP_SUCCESS;

    // Process any pending operations
    if (priv->num_cmds > 0) { res = cswp_client_transact(client, opsCompleted); }

    priv->batch_mode = BATCH_NONE;
    return res;
}

//-----------------------------------------------------------------------------
// cswp_client_info
//
// This command is not supported.
//-----------------------------------------------------------------------------
int
cswp_client_info(cswp_client_t __attribute__((unused)) *client,
                 const char    __attribute__((unused)) *message)
{
    return CSWP_UNSUPPORTED;
}

//-----------------------------------------------------------------------------
//  cswp_set_devices
// 
//  This command is not supported.
//-----------------------------------------------------------------------------
int
cswp_set_devices(cswp_client_t __attribute__((unused)) *client,
                 unsigned      __attribute__((unused)) deviceCount,
                 const char    __attribute__((unused)) **deviceList,
                 const char    __attribute__((unused)) **deviceTypes)
{
    return CSWP_UNSUPPORTED;
}

//-----------------------------------------------------------------------------
// cswp_get_devices_complete
//-----------------------------------------------------------------------------
static int
cswp_get_devices_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_get_devices_rsp_data* getDevicesReplyData = (struct cswp_get_devices_rsp_data*)replyData;

    varint_t devCount;
    int res = cswp_decode_get_devices_response_body(priv->rsp, &devCount);
    if (res == CSWP_SUCCESS) {
        *getDevicesReplyData->deviceCount = (unsigned int)devCount;
        if (getDevicesReplyData->deviceListSize < devCount) {
            res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "Device list too small");
        } else if (getDevicesReplyData->deviceTypeSize < devCount) {
            res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "Device type list too small");
        } else {
            for (unsigned i = 0; i < (unsigned)devCount && res == CSWP_SUCCESS; ++i)
            {
                res = cswp_buffer_get_string(priv->rsp, getDevicesReplyData->deviceList[i], getDevicesReplyData->deviceListEntrySize);
                res = cswp_buffer_get_string(priv->rsp, getDevicesReplyData->deviceTypes[i], getDevicesReplyData->deviceTypeEntrySize);
            }
        }
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_get_devices
//-----------------------------------------------------------------------------
int
cswp_get_devices(cswp_client_t* client,
                 unsigned* deviceCount,
                 char** deviceList,
                 size_t deviceListSize,
                 size_t deviceListEntrySize,
                 char** deviceTypes,
                 size_t deviceTypeSize,
                 size_t deviceTypeEntrySize)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_get_devices_command(priv->cmd);

    if (res == CSWP_SUCCESS) {
        struct cswp_get_devices_rsp_data* replyData = calloc(1, sizeof(struct cswp_get_devices_rsp_data));
        replyData->deviceCount = deviceCount;
        replyData->deviceList = deviceList;
        replyData->deviceListSize = deviceListSize;
        replyData->deviceListEntrySize = deviceListEntrySize;
        replyData->deviceTypes = deviceTypes;
        replyData->deviceTypeSize = deviceTypeSize;
        replyData->deviceTypeEntrySize = deviceTypeEntrySize;
        cswp_client_add_node_for_pending_rsp(client, CSWP_GET_DEVICES, cswp_get_devices_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_get_system_description
//-----------------------------------------------------------------------------
int
cswp_get_system_description(cswp_client_t *client,
                            unsigned* descriptionFormat,
                            unsigned* descriptionSize,
                            uint8_t* descriptionDataBuffer,
                            size_t bufferSize)
{
    // We implement this on the client rather than the server since it invloves
    // blindly returning large data files.
    // We look for an SDF in ARM-DS workspace or the current directory
    char filepath[1024]; // Used to contruct file paths
    const char* wrksp_dir = getenv("WRKSP_DIR");
    const char* sdf_file = getenv("SDF_FILE");
    if (sdf_file != NULL) {
        strncpy(filepath, sdf_file, sizeof(filepath));
    } else if (wrksp_dir != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/Boards/NVidia/vera_sil/vera_sil.sdf", wrksp_dir);
    } else {
        strncpy(filepath, "./vera_sil.sdf", sizeof(filepath));
    }
    FILE* file = fopen(filepath, "rb");

    if (!file) {
        *descriptionFormat = 0; // SDF
        *descriptionSize = 0;
        // Return empty description if file is not found
        return CSWP_SUCCESS;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if ((file_size < 0) || (file_size > bufferSize)) {
        fclose(file);
        return CSWP_FAILED;
    }

    size_t bytes_read = fread(descriptionDataBuffer, 1, file_size, file);
    *descriptionFormat = 0; // SDF
    *descriptionSize = bytes_read;
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        return CSWP_FAILED;
    }
    return CSWP_SUCCESS;
}

//-----------------------------------------------------------------------------
// cswp_device_open_complete
//-----------------------------------------------------------------------------
static int
cswp_device_open_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_device_open_rsp_data* deviceOpenReplyData = (struct cswp_device_open_rsp_data*)replyData;

    int res = cswp_decode_device_open_response_body(priv->rsp,
                                                   deviceOpenReplyData->deviceInfo,
                                                   deviceOpenReplyData->deviceInfoSize);
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_open
//-----------------------------------------------------------------------------
int
cswp_device_open(cswp_client_t* client,
                 unsigned deviceNo,
                 char* deviceInfo,
                 size_t deviceInfoSize)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_device_open_command(priv->cmd, deviceNo);

    if (res == CSWP_SUCCESS) {
        struct cswp_device_open_rsp_data* replyData = calloc(1, sizeof(struct cswp_device_open_rsp_data));
        replyData->deviceInfo = deviceInfo;
        replyData->deviceInfoSize = deviceInfoSize;
        cswp_client_add_node_for_pending_rsp(client, CSWP_DEVICE_OPEN, cswp_device_open_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_close
//-----------------------------------------------------------------------------
int
cswp_device_close(cswp_client_t* client, unsigned deviceNo)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_device_close_command(priv->cmd, deviceNo);
    if (res == CSWP_SUCCESS) {
        cswp_client_add_node_for_pending_rsp(client, CSWP_DEVICE_CLOSE, NULL, 0);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_set_config
//
// Thic command is not supported.
//-----------------------------------------------------------------------------
int
cswp_set_config(cswp_client_t __attribute__((unused)) *client,
                varint_t      __attribute__((unused)) deviceNo,
                const char    __attribute__((unused)) *name,
                const char    __attribute__((unused)) *value)
{
    return CSWP_UNSUPPORTED;
}

//-----------------------------------------------------------------------------
// cswp_get_config
//
// This command is not supported.
//-----------------------------------------------------------------------------
int
cswp_get_config(cswp_client_t __attribute__((unused)) *client,
                varint_t      __attribute__((unused)) deviceNo,
                const char    __attribute__((unused)) *name,
                char          __attribute__((unused)) *value,
                size_t        __attribute__((unused)) valueSize)
{
    return CSWP_UNSUPPORTED;
}

//-----------------------------------------------------------------------------
// cswp_get_device_capabilities_complete
//-----------------------------------------------------------------------------
static int
cswp_get_device_capabilities_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_get_device_capabilities_rsp_data* getDeviceCapabilitiesReplyData =
        (struct cswp_get_device_capabilities_rsp_data*)replyData;

    varint_t capabilities, capabilityData;
    int res = cswp_decode_get_device_capabilities_response_body(priv->rsp,
                                                                &capabilities,
                                                                &capabilityData);
    if (res == CSWP_SUCCESS) {
        *getDeviceCapabilitiesReplyData->capabilities = (unsigned int)capabilities;
        *getDeviceCapabilitiesReplyData->capabilityData = (unsigned int)capabilityData;
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_get_device_capabilities
//-----------------------------------------------------------------------------
int
cswp_get_device_capabilities(cswp_client_t* client,
                             varint_t deviceNo,
                             unsigned* capabilities,
                             unsigned* capabilityData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_get_device_capabilities_command(priv->cmd, deviceNo);

    if (res == CSWP_SUCCESS) {
        struct cswp_get_device_capabilities_rsp_data* replyData = calloc(1, sizeof(struct cswp_get_device_capabilities_rsp_data));
        replyData->capabilities = capabilities;
        replyData->capabilityData = capabilityData;
        cswp_client_add_node_for_pending_rsp(client, CSWP_GET_DEVICE_CAPABILITIES, cswp_get_device_capabilities_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}
//-----------------------------------------------------------------------------
// cswp_device_reg_list_complete
//-----------------------------------------------------------------------------
static int
cswp_device_reg_list_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_reg_list_rsp_data* regListReplyData = (struct cswp_reg_list_rsp_data*)replyData;

    varint_t regCount;
    int res = cswp_decode_reg_list_response_body(priv->rsp, &regCount);

    if (res == CSWP_SUCCESS) {
        *regListReplyData->registerCount = (unsigned int)regCount;
        if (regListReplyData->registerInfoSize < regCount)
            res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "registerInfo too small");
    }

    if (res == CSWP_SUCCESS) {
        /* get register info */
        const size_t nameSize = 256;
        const size_t descSize = 1024;
        char* regName = malloc(nameSize);
        char* displayName = malloc(nameSize);
        char* description = malloc(descSize);
        for (unsigned i = 0; i < (unsigned)regCount && res == CSWP_SUCCESS; ++i) {
            varint_t regID, regSize;
            res = cswp_decode_reg_info(priv->rsp, &regID,
                                       regName, nameSize,
                                       &regSize,
                                       displayName, nameSize,
                                       description, descSize);
            if (res == CSWP_SUCCESS) {
				size_t len;
                regListReplyData->registerInfo[i].id = (unsigned int)regID;
                regListReplyData->registerInfo[i].size = (unsigned int)regSize;

                len = strlen(regName);
                if (len > regListReplyData->strBufSize) {
                    res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "strBuf too small");
                } else {
                    strcpy(regListReplyData->strBuf, regName);
                    regListReplyData->registerInfo[i].name = regListReplyData->strBuf;
                    regListReplyData->strBuf += len+1;
                    regListReplyData->strBufSize -= len+1;
                }

                len = strlen(displayName);
                if (len > regListReplyData->strBufSize) {
                    res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "strBuf too small");
                } else {
                    strcpy(regListReplyData->strBuf, displayName);
                    regListReplyData->registerInfo[i].displayName = regListReplyData->strBuf;
                    regListReplyData->strBuf += len+1;
                    regListReplyData->strBufSize -= len+1;
                }

                len = strlen(description);
                if (len > regListReplyData->strBufSize) {
                    res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "strBuf too small");
                } else {
                    strcpy(regListReplyData->strBuf, description);
                    regListReplyData->registerInfo[i].description = regListReplyData->strBuf;
                    regListReplyData->strBuf += len+1;
                    regListReplyData->strBufSize -= len+1;
                }
            }
        }
        free(regName);
        free(displayName);
        free(description);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_reg_list
//-----------------------------------------------------------------------------
int
cswp_device_reg_list(cswp_client_t* client,
                     unsigned deviceNo,
                     unsigned* registerCount,
                     cswp_register_info_t* registerInfo,
                     size_t registerInfoSize,
                     char *strBuf,
                     size_t strBufSize)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_reg_list_command(priv->cmd, deviceNo);

    if (res == CSWP_SUCCESS) {
        struct cswp_reg_list_rsp_data* replyData = calloc(1, sizeof(struct cswp_reg_list_rsp_data));
        replyData->registerCount = registerCount;
        replyData->registerInfo = registerInfo;
        replyData->registerInfoSize = registerInfoSize;
        replyData->strBuf = strBuf;
        replyData->strBufSize = strBufSize;
        cswp_client_add_node_for_pending_rsp(client, CSWP_REG_LIST, cswp_device_reg_list_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_reg_read_complete
//-----------------------------------------------------------------------------
static int
cswp_device_reg_read_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_reg_read_rsp_data* regReadReplyData = (struct cswp_reg_read_rsp_data*)replyData;

    varint_t count;
    int res = cswp_decode_reg_read_response_body(priv->rsp, &count);

    if (res == CSWP_SUCCESS) {
        if (regReadReplyData->registerValuesSize < count) {
            res = cswp_client_error(client, CSWP_OUTPUT_BUFFER_OVERFLOW, "registerValues too small");
        } else {
            for (unsigned i = 0; i < (unsigned int)count && res == CSWP_SUCCESS; ++i)
                res = cswp_buffer_get_uint32(priv->rsp, &regReadReplyData->registerValues[i]);
        }
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_reg_read
//-----------------------------------------------------------------------------
int
cswp_device_reg_read(cswp_client_t* client,
                     unsigned deviceNo,
                     size_t registerCount,
                     const uint64_t* registerIDs,
                     uint32_t* registerValues,
                     size_t registerValuesSize)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    
    varint_t *regIDs = malloc(registerCount * sizeof(varint_t));
    for (unsigned i = 0; i < registerCount; ++i)
        regIDs[i] = registerIDs[i];

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_reg_read_command(priv->cmd, deviceNo, registerCount, regIDs);
    free(regIDs);

    if (res == CSWP_SUCCESS) {
        struct cswp_reg_read_rsp_data* replyData = calloc(1, sizeof(struct cswp_reg_read_rsp_data));
        replyData->registerValues = registerValues;
        replyData->registerValuesSize = registerValuesSize;
        cswp_client_add_node_for_pending_rsp(client, CSWP_REG_READ, cswp_device_reg_read_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_reg_write
//-----------------------------------------------------------------------------
int
cswp_device_reg_write(cswp_client_t* client,
                      unsigned deviceNo,
                      size_t registerCount,
                      const uint64_t* registerIDs,
                      const uint32_t* registerValues,
                      size_t __attribute__((unused)) registerValuesSize)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_reg_write_command(priv->cmd, deviceNo, registerCount);

    for (unsigned i = 0; i < registerCount; ++i) {
        cswp_buffer_put_varint(priv->cmd, registerIDs[i]);
        cswp_buffer_put_uint32(priv->cmd, registerValues[i]);
    }
    if (res == CSWP_SUCCESS) {
        cswp_client_add_node_for_pending_rsp(client, CSWP_REG_WRITE, NULL, 0);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_mem_read_complete
//-----------------------------------------------------------------------------
static int
cswp_device_mem_read_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_mem_read_rsp_data* memReadReplyData = (struct cswp_mem_read_rsp_data*)replyData;

    varint_t bytesRead;
    int res = cswp_decode_mem_read_response_body(priv->rsp, &bytesRead);
    if (res == CSWP_SUCCESS) {
        void* pData;
        cswp_buffer_get_direct(priv->rsp, &pData, bytesRead);
        memcpy(memReadReplyData->buf, pData, bytesRead);
        *memReadReplyData->bytesRead = bytesRead;
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_mem_read
//-----------------------------------------------------------------------------
int
cswp_device_mem_read(cswp_client_t* client,
                     unsigned deviceNo,
                     uint64_t address,
                     size_t size,
                     cswp_access_size_t accessSize,
                     unsigned flags,
                     uint8_t* buf,
                     size_t* bytesRead)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_mem_read_command(priv->cmd, deviceNo, address, size, accessSize, flags);
    if (res == CSWP_SUCCESS) {
        struct cswp_mem_read_rsp_data* replyData = calloc(1, sizeof(struct cswp_mem_read_rsp_data));
        replyData->buf = buf;
        replyData->bytesRead = bytesRead;
        cswp_client_add_node_for_pending_rsp(client, CSWP_MEM_READ, cswp_device_mem_read_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_mem_write
//-----------------------------------------------------------------------------
int
cswp_device_mem_write(cswp_client_t* client,
                      unsigned deviceNo,
                      uint64_t address,
                      size_t size,
                      cswp_access_size_t accessSize,
                      unsigned flags,
                      const uint8_t* pData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_mem_write_command(priv->cmd, deviceNo, address, size, accessSize, flags, pData);
    if (res == CSWP_SUCCESS) {
        cswp_client_add_node_for_pending_rsp(client, CSWP_MEM_WRITE, NULL, 0);
        res = cswp_client_send_to_transact(client); 
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_mem_poll_complete
//-----------------------------------------------------------------------------
static int
cswp_device_mem_poll_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_mem_poll_rsp_data* memPollReplyData = (struct cswp_mem_poll_rsp_data*)replyData;

    varint_t bytesRead;
    int res = cswp_decode_mem_poll_response_body(priv->rsp, &bytesRead);
    if (res == CSWP_SUCCESS) {
        void* pData;
        cswp_buffer_get_direct(priv->rsp, &pData, bytesRead);
        if (memPollReplyData->buf)       { memcpy(memPollReplyData->buf, pData, bytesRead); }
        if (memPollReplyData->bytesRead) { *memPollReplyData->bytesRead = bytesRead; }
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_device_mem_poll
//-----------------------------------------------------------------------------
int
cswp_device_mem_poll(cswp_client_t* client,
                     unsigned deviceNo,
                     uint64_t address,
                     size_t size,
                     cswp_access_size_t accessSize,
                     unsigned flags,
                     unsigned tries,
                     unsigned interval,
                     const uint8_t* mask,
                     const uint8_t* value,
                     uint8_t* buf,
                     size_t* bytesRead)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_mem_poll_command(priv->cmd, deviceNo,
                                           address, size, accessSize, flags,
                                           tries, interval, mask, value);
    if (res == CSWP_SUCCESS) {
        struct cswp_mem_poll_rsp_data* replyData = calloc(1, sizeof(struct cswp_mem_poll_rsp_data));
        replyData->buf = buf;
        replyData->bytesRead = bytesRead;
        cswp_client_add_node_for_pending_rsp(client, CSWP_MEM_POLL, cswp_device_mem_poll_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_unlock_complete
//-----------------------------------------------------------------------------
static int
cswp_nvsec_unlock_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_nvsec_unlock_rsp_data* nvsecunlockReplyData = (struct cswp_nvsec_unlock_rsp_data*)replyData;

    varint_t num_bytes = nvsecunlockReplyData->num_token_req_bytes;
    void*    pData;
    int      res       = CSWP_SUCCESS;
    
    if (num_bytes != CSWP_NVSEC_NUM_TOKEN_REQ_BYTES) {
        // Number of token request bytes is expected to be a fixed known number
        return CSWP_NVSEC_TOKEN_REQ_ERR;
    }
    
    cswp_buffer_get_direct(priv->rsp, &pData, num_bytes);
    memcpy(nvsecunlockReplyData->token_req, pData, num_bytes);

    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_unlock
//
// This command is used to unlock the CSWP Server.
//-----------------------------------------------------------------------------
int
cswp_nvsec_unlock(cswp_client_t* client,
                  uint32_t       enables,
                  uint8_t*       token_buf,
                  size_t         num_token_bytes)
{
    if (num_token_bytes != CSWP_NVSEC_NUM_TOKEN_REQ_BYTES) {
        return CSWP_NVSEC_TOKEN_REQ_ERR;
    }

    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_nvsec_unlock_command(priv->cmd, enables);

    if (res == CSWP_SUCCESS) {
        struct cswp_nvsec_unlock_rsp_data* replyData = calloc(1, sizeof(struct cswp_nvsec_unlock_rsp_data));
        replyData->token_req           = token_buf;
        replyData->num_token_req_bytes = num_token_bytes;
        cswp_client_add_node_for_pending_rsp(client, CSWP_NVSEC_UNLOCK, cswp_nvsec_unlock_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }

    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_token
//
// This command is used to provide the target with the challenge response from
// the authentication server.
//-----------------------------------------------------------------------------
int
cswp_nvsec_token(cswp_client_t* client,
                 size_t         num_bytes_signed_token,
                 uint8_t*       signed_token)
{

    if (num_bytes_signed_token != CSWP_NVSEC_NUM_SIGNED_TOKEN_BYTES) {
        return CSWP_NVSEC_SIGNED_TOKEN_ERR;
    }

    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_nvsec_token_command(priv->cmd, num_bytes_signed_token, signed_token);
    if (res == CSWP_SUCCESS) {
        cswp_client_add_node_for_pending_rsp(client, CSWP_NVSEC_TOKEN, NULL, 0);
        res = cswp_client_send_to_transact(client);
    }

    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_verify_complete
//-----------------------------------------------------------------------------
static int
cswp_nvsec_verify_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_nvsec_verify_rsp_data* rsp_node = (struct cswp_nvsec_verify_rsp_data*)replyData;

    int res = cswp_decode_nvsec_verify_response_body(priv->rsp,
                                                     rsp_node->signatureSize,
                                                     rsp_node->signature);
    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_verify
//
// This command is used to used to verify the CSWP server is attached to a
// trusted PSC firmware.
//
// Challenge: 256 bits challenge encodes as 64 hex char string
// Signature: 512 bits ECDSA signature encoded as a 128 hex char string
//-----------------------------------------------------------------------------
int
cswp_nvsec_verify(cswp_client_t* client,
                  const char*    challenge,
                  char*          signature,
                  size_t         signatureSize)
{
    if ((signatureSize+1) != CSWP_NVSEC_SIGNATURE_STR_SIZE) {
        return CSWP_NVSEC_SIGNATURE_ERR;
    }

    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_nvsec_verify_command(priv->cmd, challenge);

    if (res == CSWP_SUCCESS) {
        struct cswp_nvsec_verify_rsp_data* replyData = calloc(1, sizeof(struct cswp_nvsec_verify_rsp_data));
        replyData->signature     = signature;
        replyData->signatureSize = (signatureSize + 1);
        cswp_client_add_node_for_pending_rsp(client, CSWP_NVSEC_VERIFY, cswp_nvsec_verify_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }

    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_encrypt_complete
//-----------------------------------------------------------------------------
static int
cswp_nvsec_encrypt_complete(cswp_client_t* client, void* replyData)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;
    struct cswp_nvsec_encrypt_rsp_data* rsp_node = (struct cswp_nvsec_encrypt_rsp_data*)replyData;

    int res = cswp_decode_nvsec_encrypt_response_body(priv->rsp,
                                                      rsp_node->key_size,
                                                      rsp_node->server_public_key);
    return res;
}

//-----------------------------------------------------------------------------
// cswp_nvsec_encrypt
//
// This command is used to enable encryption of the CSWP packets.
// Public key is 4096 bits encoded as a 1024 hex char string. It is used to
// generate AES-128 key using DHKE protocol to encrypt all CSWP packets
// exchanged between client and server.
//-----------------------------------------------------------------------------
int
cswp_nvsec_encrypt(cswp_client_t* client,
                   const char*    client_public_key,
                   char*          server_public_key,
                   size_t         public_key_size)
{
    if ((public_key_size+1) != CSWP_NVSEC_PUBLIC_KEY_STR_SIZES) {
        return CSWP_NVSEC_PUBLIC_KEY_ERR;
    }
    
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    cswp_client_prep_priv_cmd_data(client);
    int res = cswp_encode_nvsec_encrypt_command(priv->cmd, client_public_key);

    if (res == CSWP_SUCCESS) {
        struct cswp_nvsec_encrypt_rsp_data* replyData = calloc(1, sizeof(struct cswp_nvsec_encrypt_rsp_data));
        replyData->server_public_key = server_public_key;
        replyData->key_size          = (public_key_size + 1);
        cswp_client_add_node_for_pending_rsp(client, CSWP_NVSEC_ENCRYPT, cswp_nvsec_encrypt_complete, replyData);
        res = cswp_client_send_to_transact(client);
    }
    return res;
}

//-----------------------------------------------------------------------------
// cswp_async_message
//
// This API is used to read out any async messages received by the client
// from server.
//-----------------------------------------------------------------------------
int
cswp_async_message(cswp_client_t* client,
                   varint_t*      deviceNo,
                   varint_t*      msg_level,
                   char*          msg,
                   size_t         msg_size)
{
    cswp_client_priv_t* priv = (cswp_client_priv_t*)client->priv;

    int res = client->receive_async(client, priv->rsp->buf, priv->rsp->size, &priv->rsp->used);

    if (res != CSWP_ASYNC_MSG_LOG) { return res; }

    // Start processing response received

    // Check header
    uint32_t rspSize;
    cswp_buffer_seek(priv->rsp, 0); // Reset the buff read pointer to start
    // Check response size aka message length mataches the data received.
    cswp_buffer_get_uint32(priv->rsp, &rspSize);
    if (rspSize > priv->rsp->used) {
        return cswp_client_error(client, CSWP_RESPONSE_SIZE_ERR,
                                "Incomplete response received.  Received %d bytes, expected %d",
                                priv->rsp->used, rspSize);
    }

    // Check that all responses are received
    varint_t numRsps;
    cswp_buffer_get_varint(priv->rsp, &numRsps);
    if (numRsps != 1) {
        return cswp_client_error(client, CSWP_RESPONSE_SIZE_ERR,
                "Async response is expected to be received as a single response but received %d responses.", numRsps);
    }

    // Verify msgType, check errorcode and decode async data
    varint_t msgType, errCode;
    cswp_buffer_get_varint(priv->rsp, &msgType);
    if (msgType != CSWP_ASYNC_MESSAGE) {
        return cswp_client_error(client, CSWP_UNEXPECTED_RESPONSE, "Unexpected response: 0x%lX", msgType);
    }
            
    cswp_buffer_get_varint(priv->rsp, &errCode);
    if ((errCode != ASYNC_RAS_EVENT) && (errCode != ASYNC_DBG_PUTS)) {
        cswp_decode_error_response_body(priv->rsp, client->errorMsg, CSWP_ERR_MSG_SIZE);
        return (int)errCode;
    }

    res = cswp_decode_async_message_body(priv->rsp,
                                        deviceNo, msg_level, msg, msg_size);
    if (res == CSWP_SUCCESS) { res = CSWP_ASYNC_MSG_LOG; }

    return res;
}

//-----------------------------------------------------------------------------
// cswp_decode_error
//
// Return a string describing the error code in e.
//-----------------------------------------------------------------------------
const char *
cswp_decode_error(cswp_client_t* client, int e)
{
    cswp_result_t cswp_e = (cswp_result_t)e;
    switch(cswp_e) {
        case CSWP_SUCCESS: return "Successful operation";
        case CSWP_FAILED: return "Other error";
        case CSWP_CANCELLED: return "Not executed due to previous failure";
        case CSWP_NOT_INITIALIZED: return "Not initialized";
        case CSWP_ASYNC_MSG_LOG: return "Client: Response with asyncQ message";
        case CSWP_ASYNC_MSG_END: return "Client: asyncQ is empty";
        case CSWP_NVSEC_TOKEN_REQ_ERR: return "Client: NVSEC token error";
        case CSWP_NVSEC_SIGNED_TOKEN_ERR: return "Client: NVSEC token error";
        case CSWP_NVSEC_SIGNATURE_ERR: return "Client: NVSEC signature error";
        case CSWP_NVSEC_PUBLIC_KEY_ERR: return "Client:  NVSEC public key error";
        case CSWP_BUFFER_FULL: return "Insufficient space in CSWP_BUFFER when encoding";
        case CSWP_BUFFER_EMPTY: return "Insufficient data left in CSWP_BUFFER when decoding";
        case CSWP_OUTPUT_BUFFER_OVERFLOW: return "Insufficient space in output buffer when decoding";
        case CSWP_UNEXPECTED_RESPONSE: return "Client: Response check failed for response type";
        case CSWP_RESPONSE_SIZE_ERR: return "Client: Response check failed for size";
        case CSWP_COMMS: return "Communication error";
        case CSWP_INCOMPATIBLE: return "The server is not compatible with the client";
        case CSWP_TIMEOUT: return "A timeout occurred executing a command";
        case CSWP_UNSUPPORTED: return "Command unsupported";
        case CSWP_DEVICE_UNSUPPORTED: return "Unsupported device";
        case CSWP_INVALID_DEVICE: return "Invalid device ID";
        case CSWP_BAD_ARGS: return "Bad arguments to command";
        case CSWP_NOT_PERMITTED: return "Operation not permitted";
        case CSWP_REG_FAILED: return "Register access failed";
        case CSWP_REG_PARTIAL: return "Attempt to access part of a multiple element register";
        case CSWP_MEM_FAILED: return "Memory access failed";
        case CSWP_MEM_INVALID_ADDRESS: return "Invalid address for memory access";
        case CSWP_MEM_BAD_ACCESS_SIZE: return "Invalid access size for memory access";
        case CSWP_MEM_POLL_NO_MATCH: return "Poll did not match";
    }
    return "Invalid error code";
}

// End of cswp_client.c
