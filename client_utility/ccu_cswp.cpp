/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

// This file contains wrapper function for CSWP-UTIL interface.

#include <iostream>
#include <cstdio>
#include <cstring>
#include <string>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <fstream>
#include <libgen.h>

#include "ccu_in.h"
#include "ccu_out.h"
#include "ccu.h"
#include "cswp_client_plat_if.h"
#include "cstrace_plat_if.h"
#include "cswp_client_ex.h"
#include "cstrace_ex.h"

using namespace std;

extern char *prog_name;

/*
 * ---------------------------------------------------------------------------------------------------
 * Utility macros
 * ---------------------------------------------------------------------------------------------------
*/
#define _UNUSED_PARAM_       __attribute__((unused))
#define MAX_NUM_DEVICES       3U
#define MAX_DEV_NAME_LEN     50U
#define MAX_DEV_TYPE_LEN     30U
#define MAX_SERVER_NAME_LEN  50U
#define CLIENT_PROTOCOL_V    0x100U
#define CLIENT_ID_STR        "CSWP_CLIENT_UTIL"
#define MAX_SYS_DESC_LEN     5242880U // 5MB
#define WAIT_FOR_EVENT_MS    1000
#define CSTRACE_UPDATE_MS    1000   // Time between trace update messages

#define TRAP_EX_CCU(block)                                                   \
   ({                                                                        \
   int status = 0;                                                           \
   try {                                                                     \
       block;                                                                \
   } catch (const CSTraceEx& e) {                                            \
       status = (int)e.code();                                               \
       cout << "[ccu_cswp] Streaming trace exception: " << e.what() << endl; \
   } catch (const CDebugEx& e) {                                             \
       status = (int)e.code();                                               \
       cout << "[ccu_cswp] CSWP Debug exception: " << e.what() << endl;      \
   } catch (const USBException& e) {                                         \
       status = CCU_USB_RUNTIME_EXCEPTION;                                   \
       cout << "[ccu_cswp] USB runtime exception: " << e.what() <<endl;      \
   } catch (...) {                                                           \
       status = CCU_UNKNOWN_EXCEPTION;                                       \
       cout << "[ccu_cswp] Unknown exception occurred." << endl;             \
   }                                                                         \
   status;                                                                   \
   })

#define RETURN_EARLY_ON_ERR(condition, msg, errcode)  \
    do {                                              \
        if (condition) {                              \
            cout << "[ccu_cswp] " << msg << endl;     \
            return errcode;                           \
        }                                             \
    } while (0)

#define PRINT_PROGRESS_STAT(msg)                      \
    do {                                              \
        cout << "[ccu_stat] " << msg << endl;         \
    } while (0)

/*
 * -------------------------------------------------------------------
 * Handlers for ctrl+c interrupt from user to stop tracing or
 * async message collection.
 * -------------------------------------------------------------------
*/
volatile int exitFlag = 0;
static void
exit_handler(int sig) {
    exitFlag = 1;
    signal(sig, SIG_IGN);
    PRINT_PROGRESS_STAT("Caught ctrl+c.. exiting..");
}
static bool
setup_user_sigint_handler(void) {
    void (*old_handler)(int) = signal(SIGINT, exit_handler);
    if (old_handler == SIG_ERR) { return false; }
    exitFlag = 0;
    return true;
}
static bool
restore_sigint_handler(void) {
    void (*old_handler)(int) = signal(SIGINT, SIG_DFL);
    if (old_handler == SIG_ERR) { return false; }
    return true;
}

static long
get_current_time(void) {
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) { return 0; }
    return ((tv.tv_sec*1000) + 
            (tv.tv_usec/1000) + 
            ((tv.tv_usec >= 500) ? 1 : 0));
}

/*
 * ---------------------------------------------------------------------------------------------------
 * parse_transport_cfg_file
 * ---------------------------------------------------------------------------------------------------
*/
static int
parse_transport_cfg_file(transport_cfg_t* t_info, const char* cfg_file) {

    map<string, string> cfg_map;

    ifstream file(cfg_file);
#ifdef CONFIG_FILES_PATH
    if (!file.is_open()) {
        char *tmp = strdup(prog_name);
        char *prog_dir_name = dirname(tmp);
        char *cfg_file_name = (char *)malloc(strlen(prog_dir_name) + strlen("/" CONFIG_FILES_PATH "/") + strlen(cfg_file) + 1);
        strcpy(cfg_file_name, prog_dir_name);
        strcat(cfg_file_name, "/" CONFIG_FILES_PATH "/");
        strcat(cfg_file_name, cfg_file);
        file = ifstream(cfg_file_name);
        free(cfg_file_name);
        free(tmp);
        RETURN_EARLY_ON_ERR((!file.is_open()), "File not found in the current directory or " CONFIG_FILES_PATH " (relative to the ccu utility path)." , CCU_CFG_FILE_ERR);
    }
#else
    RETURN_EARLY_ON_ERR((!file.is_open()), "File not found in current directory.", CCU_CFG_FILE_ERR);
#endif

    string line;
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') // Skip empty lines and comments
            continue;
        // Parse key=value pairs
        size_t equalPos = line.find('=');
        if (equalPos != string::npos) {
            string key = line.substr(0, equalPos);
            string value = line.substr(equalPos + 1);
            cfg_map[key] = value;
        }
    }

    RETURN_EARLY_ON_ERR((cfg_map.empty() || (cfg_map.count("type") <= 0)),
                        "Please make sure to set appropriate transport config data in the cfg file in this directory.",
                         CCU_CFG_FILE_ERR);

    // Extract cfguration data
    t_info->type = cfg_map["type"];
    RETURN_EARLY_ON_ERR(((t_info->type != "usb") && (t_info->type != "tcp")),
                        "Transport type can only be tcp or usb. Please check in the cfg gile in this directory.",
                        CCU_CFG_FILE_ERR);

    t_info->vid    = 0;
    t_info->pid    = 0;
    t_info->iid    = 0;
    t_info->sid    = 0;
    t_info->serial = "";
    t_info->ipaddr = "";
    t_info->portid = 8080;
    if (t_info->type == "tcp") {
        RETURN_EARLY_ON_ERR(cfg_map.find("ipaddr") == cfg_map.end(),
                            "You must specify an IP to attach to in TCP mode with 'ipaddr=<ip>'.",
                            CCU_CFG_FILE_ERR);
        t_info->ipaddr = cfg_map["ipaddr"];
        if (cfg_map.find("port") != cfg_map.end())
            t_info->portid = stoi(cfg_map["port"]);
    } else if (t_info->type == "usb") {
        RETURN_EARLY_ON_ERR(cfg_map.find("vid") == cfg_map.end(),
                            "You must specify the VID of the device to attach in USB mode with 'vid=<vid>'.",
                            CCU_CFG_FILE_ERR);
        t_info->vid = stoi(cfg_map["vid"], nullptr, 16);
        RETURN_EARLY_ON_ERR(cfg_map.find("pid") == cfg_map.end(),
                            "You must specify the PID of the device to attach in USB mode with 'pid=<pid>'.",
                            CCU_CFG_FILE_ERR);
        t_info->pid = stoi(cfg_map["pid"], nullptr, 16);
        if (cfg_map.find("iid") != cfg_map.end())
            t_info->iid = stoi(cfg_map["iid"], nullptr, 16);
        if (cfg_map.find("sid") != cfg_map.end())
            t_info->sid = stoi(cfg_map["sid"], nullptr, 16);
        if (cfg_map.find("serial") != cfg_map.end())
            t_info->serial = cfg_map["serial"];
    }

    return 0;
}

/*
 * -------------------------------------------------------------------
 * setup_cdebug
 * 
 * Since this utility exits after completing ccu command, this
 * function needs to be called to re-establish transport at the start
 * of every command. In case of init_cswp connect happens as part of
 * command processing so it is skipped and only CSWP instance is
 * created.
 * -------------------------------------------------------------------
*/
extern CSWPBase* CreateCSWP(transport_cfg_t transport_cfg);
static int
setup_cdebug(unique_ptr<CSWPBase>& cdbgp, bool set_con) {

    int res, ex_stat;
    transport_cfg_t transport_info;

    res = parse_transport_cfg_file(&transport_info, "cdebug_trnsprt_cfg.txt");
    RETURN_EARLY_ON_ERR((res != 0), "Error finding or parsing cfg file cdebug_trnsprt_cfg.txt.", res);

    // Create CSWP client class object
    ex_stat = TRAP_EX_CCU (cdbgp.reset(CreateCSWP(transport_info)));
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while creating CSWP USB Client.", ex_stat);

    // Claim and connect to USB interface 0
    if (set_con) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->connect());
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while connecting to USB.", ex_stat);
    }
    return res;
}

/*
 * -------------------------------------------------------------------
 * flush_async_data_queue
 *
 * Flushes any data that the async client queue may have.
 * -------------------------------------------------------------------
*/
static int
flush_async_data_queue(unique_ptr<CSWPBase>& cdbgp) {
    varint_t deviceNo;
    varint_t msg_level;
    char     msg[MAX_ASYNC_MSG_LEN];
    size_t   msg_size = MAX_ASYNC_MSG_LEN;

    while (1) {
        int res = cdbgp->cswp_async_message(&deviceNo, &msg_level, msg, msg_size);
        if (res == CSWP_ASYNC_MSG_LOG) {
            process_async_message_out(deviceNo, msg_level, msg);
        } else {
            RETURN_EARLY_ON_ERR((res != CSWP_ASYNC_MSG_END),
                                "Async message received has error. Ignoring and proceeding to disconnecting transport.", res);
            break;
        }
    }
    return 0;
}

/*
 * -------------------------------------------------------------------
 * disconnect_transport
 * -------------------------------------------------------------------
*/
static int
disconnect_transport(unique_ptr<CSWPBase>& cdbgp) {
    int ex_stat = TRAP_EX_CCU (cdbgp->disconnect());
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while disconnecting USB.", ex_stat);
    return 0;
}

/*
 * -------------------------------------------------------------------
 * cleanup_cdebug
 * 
 * Since this utility exits after each command, you need disconnect
 * transport except for term_cswp for which it is part of the process.
 * -------------------------------------------------------------------
*/
static int
cleanup_cdebug(unique_ptr<CSWPBase>& cdbgp, bool async_flush_and_discon_transport) {
    if (async_flush_and_discon_transport) {
        int res1 = flush_async_data_queue(cdbgp);
        int res2 = disconnect_transport(cdbgp);
        if (res2 != 0) { return res2; }
        if (res1 != 0) { return res1; }
    }
    return 0;
}

#define TERM_EARLY_ON_ERR(cdbgp, condition, msg, errcode)                       \
    do {                                                                        \
        if (condition) {                                                        \
            cout << "[ccu_cswp] " << msg << endl;                               \
            cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */); \
            return errcode;                                                     \
        }                                                                       \
    } while (0)

static int
cleanup_cdebug_wo_async_flush(unique_ptr<CSWPBase>& cdbgp) {
    int res = disconnect_transport(cdbgp);
    if (res != 0) { return res; }
    return 0;
}

#define TERM_EARLY_ON_ERR_WO_ASYNC_FLUSH(cdbgp, condition, msg, errcode)     \
    do {                                                                     \
        if (condition) {                                                     \
            cout << "[ccu_cswp] " << msg << endl;                            \
            cleanup_cdebug_wo_async_flush(cdbgp);                            \
            return errcode;                                                  \
        }                                                                    \
    } while (0)

/*
 * -------------------------------------------------------------------
 * alloc_cstrace_runtime_storage
 * 
 * Allocates client trace buffers for data and event
 * -------------------------------------------------------------------
*/
static int
alloc_cstrace_runtime_storage(cstrace_cmd_t* c) {
    
    SinkDetails sink = c->tracep->GetSinkDetails(c->etrID);

    size_t size_Dbufs    = sink.dataBufferSize;
    size_t size_Ebufs    = sink.eventBufferSize;
    c->num_Dbufs         = sink.dataBufferCount;
    c->num_Ebufs         = sink.eventBufferCount;
    c->num_tbufs         = c->num_Dbufs + c->num_Ebufs;
    c->num_pending_bufs  = 0;
    c->num_capture_bytes = 0;

    // Allocate an array to hold pointers of all the client trace buffer information data structures
    c->tbufs_info = (CSTraceEventBuffer*) malloc (c->num_tbufs * sizeof(CSTraceEventBuffer));
    RETURN_EARLY_ON_ERR((c->tbufs_info == NULL), "ccu trace buffer pointer array alloc failed.", CCU_MALLOC_ERR);

    // Allocate an array to hold pointers of buffer tokens
    c->tbufs_req_tokens = (int*) malloc (c->num_tbufs * sizeof(int));
    RETURN_EARLY_ON_ERR((c->tbufs_req_tokens == NULL), "ccu trace token buffer pointer array alloc failed.", CCU_MALLOC_ERR);

    // Allocate client trace data and event buffers, store their information in tbufs_info array, initialize tbufs_req_tokens to -1
    int err = 0;
    for (size_t i = 0; i < c->num_Dbufs; ++i) {
        c->tbufs_info[i].pBuf  = (uint8_t*) malloc (size_Dbufs);
        if (c->tbufs_info[i].pBuf == NULL) { err = -1; break; }
        c->tbufs_info[i].size  = size_Dbufs;
        c->tbufs_req_tokens[i] = -1;
    }
    RETURN_EARLY_ON_ERR((err != 0), "ccu trace data buffer alloc failed.", CCU_MALLOC_ERR);
    
    for (size_t i = c->num_Dbufs; i < c->num_tbufs; ++i) {
        c->tbufs_info[i].pBuf  = (uint8_t*) malloc (size_Ebufs);
        if (c->tbufs_info[i].pBuf == NULL) { err = -1; break; }
        c->tbufs_info[i].size  = size_Ebufs;
        c->tbufs_req_tokens[i] = -1;
    }
    RETURN_EARLY_ON_ERR((err != 0), "ccu trace event buffer alloc failed.", CCU_MALLOC_ERR);

    return 0;
}

/*
 * -------------------------------------------------------------------
 * setup_cstrace
 * -------------------------------------------------------------------
*/
extern StreamingTraceBase* CreateStreamingTrace(transport_cfg_t transport_cfg);
static int
setup_cstrace(cstrace_cmd_t* c) {

    int ex_stat;
    transport_cfg_t transport_info;

    int res = parse_transport_cfg_file(&transport_info, "cstrace_trnsprt_cfg.txt");
    RETURN_EARLY_ON_ERR((res != 0), "Error finding or parsing cfg file cstrace_trnsprt_cfg.txt.", res);
    RETURN_EARLY_ON_ERR((transport_info.type != "usb"), "Trace is only supported over usb.", CCU_CFG_FILE_ERR);

    // Create Streaming Trace USB Client and claim interface 1
    ex_stat = TRAP_EX_CCU (c->tracep.reset(CreateStreamingTrace(transport_info)));
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while creating Streaming trace USB Client.", ex_stat);
    RETURN_EARLY_ON_ERR((c->tracep  == NULL), "Could not create Streaming trace USB Client.", CCU_MALLOC_ERR);
    
    // Claim and connect to USB interface 1
    // There is no need to separately claim interface 0 to send control reqs.
    ex_stat = TRAP_EX_CCU (c->tracep->Connect());
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while connecting to USB.", ex_stat);

    // Send Id of the sink to server to start attachment
    ex_stat = TRAP_EX_CCU (c->tracep->Attach(c->etrID));
    if (ex_stat != 0 ) {
        c->tracep->Disconnect();
        RETURN_EARLY_ON_ERR(true, "Error while attaching sink.", ex_stat);
    }

    int ready_status = alloc_cstrace_runtime_storage(c);
    if (ready_status != 0) {
        c->tracep->Detach(c->etrID);
        c->tracep->Disconnect();
        RETURN_EARLY_ON_ERR(true, "Error while allocating runtime storage.", ready_status);
    }

    return 0;
}

/*
 * -------------------------------------------------------------------
 * cleanup_cstrace
 * -------------------------------------------------------------------
*/
static int
cleanup_cstrace(cstrace_cmd_t* c) {

    int ex_stat = 0;
    ex_stat = TRAP_EX_CCU (c->tracep->Detach(c->etrID));
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while detaching sink.", ex_stat);

    ex_stat = TRAP_EX_CCU (c->tracep->Disconnect());
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while disconnecting USB.", ex_stat);

    // Free all runtime storage
    for (size_t i = 0; i < c->num_tbufs; ++i) { free(c->tbufs_info[i].pBuf); }
    free(c->tbufs_info);
    free(c->tbufs_req_tokens);

    return 0;
}

/*
 * -------------------------------------------------------------------
 * init_cswp
 * -------------------------------------------------------------------
*/
int
init_cswp(int argc _UNUSED_PARAM_, char* argv[] _UNUSED_PARAM_) {

    RETURN_EARLY_ON_ERR((argc != 2), "Exepcted 2 arguments.", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, false /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    const char clientID[20]    = CLIENT_ID_STR;
    uint64_t   ProtocolVersion = CLIENT_PROTOCOL_V;
    size_t     serverIDSize    = MAX_SERVER_NAME_LEN;
    // Output
    uint64_t serverProtocolVersion = 0;
    char     serverID[MAX_SERVER_NAME_LEN];
    uint32_t serverVersion = 0;

    if (res == CSWP_SUCCESS) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_init (clientID,
                                                       ProtocolVersion,
                                                       &serverProtocolVersion,
                                                       serverID, serverIDSize,
                                                       &serverVersion));
        
    }
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught CSWP_INIT.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_INIT command failed.", res);
    process_init_cswp_out(serverProtocolVersion, serverID, serverVersion);

    // Open all devices
    char deviceInfoList_2D[MAX_NUM_DEVICES][MAX_DEV_NAME_LEN];
    char * deviceInfoList[MAX_NUM_DEVICES];
    for (unsigned i = 0; i < MAX_NUM_DEVICES; ++i) {
        deviceInfoList[i]  = deviceInfoList_2D[i];
    }
    size_t   deviceInfoSize = MAX_DEV_NAME_LEN;
    uint32_t ops_completed  = 0;

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_begin(1 /*abort on error*/));
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in starting a batch.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "cswp_batch_begin failed.", res);

    for (uint32_t i = 0; (i < MAX_NUM_DEVICES); i++) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_open(i, deviceInfoList[i], deviceInfoSize));
        TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_device_open.", ex_stat);
        TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_DEV_OPEN command failed.", res);
    }

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_end(&ops_completed));
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in submitting batch.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "cswp_batch_end failed.", res);
    TERM_EARLY_ON_ERR(cdbgp, (ops_completed != MAX_NUM_DEVICES), "Batch ops_completed check failed.", CCU_BATCH_OPS_ERR);

    process_dev_open_out(deviceInfoList, MAX_NUM_DEVICES);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * term_cswp
 * -------------------------------------------------------------------
*/
int
term_cswp(int argc _UNUSED_PARAM_, char* argv[] _UNUSED_PARAM_) {

    RETURN_EARLY_ON_ERR((argc != 2), "Exepcted 2 arguments.", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // close all devices
    uint32_t ops_completed = 0;

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_begin(1 /*abort on error*/));
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in starting a batch.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "cswp_batch_begin failed.", res);

    for (uint32_t i=0; (i < MAX_NUM_DEVICES); i++) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_close(i));
        TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_device_close.", ex_stat);
        TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_DEV_CLOSE command failed.", res);
    }

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_end(&ops_completed));
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in submitting batch.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "cswp_batch_end failed.", res);
    TERM_EARLY_ON_ERR(cdbgp, (ops_completed != MAX_NUM_DEVICES), "Batch ops_completed check failed.", CCU_BATCH_OPS_ERR);

    process_dev_close_out();

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_term());
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in CSWP_TERM.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_TERM command failed.", res);

    process_term_cswp_out();

    res = cleanup_cdebug(cdbgp, false /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * get_dev_info
 * -------------------------------------------------------------------
*/
int
get_dev_info(int argc _UNUSED_PARAM_, char* argv[] _UNUSED_PARAM_) {

    RETURN_EARLY_ON_ERR((argc != 2), "Exepcted 2 arguments.", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    size_t deviceListSize        = MAX_NUM_DEVICES;
    size_t deviceListEntrySize   = MAX_DEV_NAME_LEN;
    size_t deviceTypeSize        = MAX_NUM_DEVICES;
    size_t deviceTypeEntrySize   = MAX_DEV_TYPE_LEN;
    // Output
    uint32_t deviceCount;
    char deviceList_2D[MAX_NUM_DEVICES][MAX_DEV_NAME_LEN];
    char deviceTypes_2D[MAX_NUM_DEVICES][MAX_DEV_TYPE_LEN];
    // This is to get char** that the function expects
    char * deviceList[MAX_NUM_DEVICES];
    char * deviceTypes[MAX_NUM_DEVICES];
    for (unsigned i = 0; i < MAX_NUM_DEVICES; ++i) {
        deviceList[i]  = deviceList_2D[i];
        deviceTypes[i] = deviceTypes_2D[i];
    }

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_get_devices(&deviceCount,
                                                         deviceList,
                                                         deviceListSize,
                                                         deviceListEntrySize,
                                                         deviceTypes,
                                                         deviceTypeSize,
                                                         deviceTypeEntrySize));
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_get_devices.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_GET_DEVICES command failed.", res);

    process_get_dev_info_out(deviceList, deviceTypes, deviceCount);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * get_dev_cap
 * -------------------------------------------------------------------
*/
int
get_dev_cap(int argc, char* argv[]) {

    dev_cmd_t dev_cmd_info;
    bool extracted = extract_get_dev_cap_param(argc, argv, &dev_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    varint_t deviceNo = dev_cmd_info.devID;
    // Output
    uint32_t capabilities;
    uint32_t capabilityData;

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_get_device_capabilities(deviceNo,
                                                                     &capabilities,
                                                                     &capabilityData));

    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_get_device_capabilities.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_GET_DEVICE_CAPABILITIES command failed.", res);

    process_get_dev_cap_out(capabilities, capabilityData);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * get_sys_info
 * -------------------------------------------------------------------
*/
int
get_sys_info(int argc _UNUSED_PARAM_, char* argv[] _UNUSED_PARAM_) {

    RETURN_EARLY_ON_ERR((argc != 2), "Exepcted 2 arguments.", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    size_t     bufferSize      = MAX_SYS_DESC_LEN;
    // Output
    unsigned descriptionFormat = 0;
    unsigned descriptionSize   = 0;
    uint8_t  descriptionDataBuffer[MAX_SYS_DESC_LEN];

    if (res == CSWP_SUCCESS) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_get_system_description(&descriptionFormat, &descriptionSize,
                                                                        descriptionDataBuffer, bufferSize));
        
    }
    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught CSWP_GET_SYSTEM_DESCRIPTION.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_GET_SYSTEM_DESCRIPTION command failed.", res);

    process_get_sys_info_out(descriptionFormat, descriptionSize, descriptionDataBuffer); 

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * get_reg_list
 * -------------------------------------------------------------------
*/
int
get_reg_list(int argc _UNUSED_PARAM_, char* argv[] _UNUSED_PARAM_) {

    RETURN_EARLY_ON_ERR((argc != 2), "Exepcted 2 arguments.", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    uint32_t              deviceNo = 0; // This is only supported for device 0
    // Output
    uint32_t              registerCount;
    cswp_register_info_t  registerInfo[DEV0_REG_COUNT];
    size_t                registerInfoSize = MAX_REG_INFO_ARR_SZ;
    char                  strBuf[MAX_REG_INFO_ARR_SZ];
    size_t                strBufSize = MAX_REG_INFO_ARR_SZ; 

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_reg_list(deviceNo,
                                                             &registerCount,
                                                             registerInfo, registerInfoSize,
                                                             strBuf, strBufSize));

    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_device_reg_list.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_REG_LIST command failed.", res);

    process_get_reg_list_out(registerCount, registerInfo);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * get_dev_reg
 * -------------------------------------------------------------------
*/
int
get_dev_reg(int argc, char* argv[]) {

    reg_cmd_t reg_cmd_info;
    bool extracted = extract_get_dev_reg_param(argc, argv, &reg_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    uint32_t deviceNo          = reg_cmd_info.devID;
    size_t   registerCount     = reg_cmd_info.regSZ;
    size_t   registerValueSize = reg_cmd_info.regSZ;
    uint64_t registerID[2];
    registerID[0] = (reg_cmd_info.regID);
    registerID[1] = (reg_cmd_info.regID+4); // Will be discarded if the size is 1
    
    // Output
    uint32_t registerValue[2];

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_reg_read(deviceNo,
                                                             registerCount,
                                                             registerID,
                                                             registerValue,
                                                             registerValueSize));

    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_device_reg_read.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_REG_READ command failed.", res);

    process_get_dev_reg_out(registerValue, registerValueSize);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * set_dev_reg
 * -------------------------------------------------------------------
*/
int
set_dev_reg(int argc, char* argv[]) {

    reg_cmd_t reg_cmd_info;
    bool extracted = extract_set_dev_reg_param(argc, argv, &reg_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int ex_stat = 0, res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    uint32_t deviceNo          = reg_cmd_info.devID;
    size_t   registerCount     = reg_cmd_info.regSZ;
    size_t   registerValueSize = reg_cmd_info.regSZ;
    uint64_t registerID[2];
    uint32_t registerValue[2];

    registerID[0]    = reg_cmd_info.regID;
    registerID[1]    = (reg_cmd_info.regID+4); // Will be discarded if the size is 1
    registerValue[0] = (uint32_t)(reg_cmd_info.value & 0xFFFFFFFF);
    registerValue[1] = (uint32_t)((reg_cmd_info.value >> 32) & 0xFFFFFFFF); // Will be discarded if the size is 1

    ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_reg_write(deviceNo,
                                                              registerCount,
                                                              registerID,
                                                              registerValue,
                                                              registerValueSize));

    TERM_EARLY_ON_ERR(cdbgp, (ex_stat != 0), "Exception caught in cswp_device_reg_write.", ex_stat);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0),     "CSWP_REG_WRITE command failed.", res);

    process_set_dev_reg_out();

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * create_mem_subreqs
 * -------------------------------------------------------------------
*/
static inline uint64_t
create_mem_subreqs(uint32_t devID, uint64_t start_addr, uint64_t rw_values_idx, uint64_t total_bytes, subreq_info* subreqs) {
    uint64_t saddr     = start_addr;
    uint64_t buf_idx   = rw_values_idx;
    uint64_t num_bytes = total_bytes;
    uint64_t i         = 0;
    while (num_bytes > 0) {
        subreqs[i].saddr   = saddr;
        subreqs[i].buf_idx = buf_idx;
        if (num_bytes >= 8) {
            subreqs[i].size   = (devID == 0) ? ((num_bytes/4) * 4) : ((num_bytes/8) * 8);
            subreqs[i].acc_sz = (devID == 0) ? CSWP_ACCESS_SIZE_32 : CSWP_ACCESS_SIZE_64;
        } else if (num_bytes >= 4) {
            subreqs[i].size   = ((num_bytes/4) * 4);
            subreqs[i].acc_sz = CSWP_ACCESS_SIZE_32;
        } else if (num_bytes >= 2) {
            // dev0 reqs will not ever fall here as num_bytes 
            // have been adjusted before calling this function
            subreqs[i].size   = ((num_bytes/2) * 2);
            subreqs[i].acc_sz = CSWP_ACCESS_SIZE_16;
        } else {
            // dev0 reqs will not ever fall here as num_bytes 
            // have been adjusted before calling this function
            subreqs[i].size   = 1;
            subreqs[i].acc_sz = CSWP_ACCESS_SIZE_8;
        }
        // Updates for next subreq
        saddr     = saddr   + subreqs[i].size;
        buf_idx   = buf_idx + subreqs[i].size;
        num_bytes = num_bytes - subreqs[i].size;
        i++;
    }
    return i;
}

/*
 * -------------------------------------------------------------------
 * execute_get_dev_mem_out
 * -------------------------------------------------------------------
*/
static int
execute_get_dev_mem_out (unique_ptr<CSWPBase>& cdbgp, mem_cmd_t* m) {
    int ex_stat = 0, res = 0;
    // Input
    unsigned           deviceNo   = m->devID;
    unsigned           flags      = (uint32_t)m->flags;
    uint64_t           buf_idx;
    uint8_t *          buf;
    uint64_t           address;
    cswp_access_size_t accessSize;
    size_t             size;
    size_t             bytesRead;

    if (m->num_subreqs > 1) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_begin(1 /*abort on error*/));
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Exception caught in starting a batch.", ex_stat);
        RETURN_EARLY_ON_ERR((res != 0),     "cswp_batch_begin failed.", res);
    }

    for (uint32_t i = 0; (i < m->num_subreqs); i++) {
        address    = m->subreqs[i].saddr;
        accessSize = m->subreqs[i].acc_sz;
        size       = m->subreqs[i].size;
        buf_idx    = m->subreqs[i].buf_idx;
        buf        = &m->rw_values[buf_idx];
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_mem_read(deviceNo, address, size, accessSize, flags,
                                                                 buf, &bytesRead));
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Exception caught in cswp_device_mem_read.", ex_stat);
        RETURN_EARLY_ON_ERR((res != 0),     "CSWP_MEM_READ command failed.", res);
    }

    if (m->num_subreqs > 1) {
        uint32_t ops_completed = 0;
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_end(&ops_completed));
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Exception caught in submitting batch.", ex_stat);
        RETURN_EARLY_ON_ERR((res != 0),     "cswp_batch_end failed.", res);
        RETURN_EARLY_ON_ERR((ops_completed != m->num_subreqs), "Batch ops_completed check failed.", CCU_BATCH_OPS_ERR);
    }

    return 0;
}

/*
 * -------------------------------------------------------------------
 * get_dev_mem
 * -------------------------------------------------------------------
*/
int
get_dev_mem(int argc, char* argv[]) {

    mem_cmd_t mem_cmd_info;
    bool extracted = extract_get_dev_mem_param(argc, argv, &mem_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // In order to ensure, size to be multiple of accesssize
    mem_cmd_info.num_subreqs = create_mem_subreqs(mem_cmd_info.devID,
                                                  mem_cmd_info.start_addr,
                                                  0, /* rw_values_idx */
                                                  mem_cmd_info.total_bytes, 
                                                  mem_cmd_info.subreqs);

    res = execute_get_dev_mem_out(cdbgp, &mem_cmd_info);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0), "Error received from execute_get_dev_mem_out." , res);

    process_get_dev_mem_out(mem_cmd_info.start_addr, mem_cmd_info.rw_values, mem_cmd_info.total_bytes);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * dump_dev_mem
 * -------------------------------------------------------------------
*/
int
dump_dev_mem(int argc, char* argv[]) {

    mem_cmd_t mem_cmd_info;
    bool extracted = extract_dump_dev_mem_param(argc, argv, &mem_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    uint64_t num_bytes     = 0;
    uint64_t rd_values_idx = 0;
    // Due to limited size of CSWP RSP buffer, one CSWP_MEM_READ can only read 16372 bytes
    // even after employing the batch method. Therefore, CCU must send multiple of such 
    // batch/single reqs to ensure user can read upto 265kB with 1 CCU command.
    // Note that subreqs are needed so that size/req is always a multiple of access_size.
    while (mem_cmd_info.total_bytes > 0) {

        if (mem_cmd_info.total_bytes >= MEM_MAX_RW_SZ) { num_bytes  = MEM_MAX_RW_SZ;  }    
        else                                           { num_bytes  = mem_cmd_info.total_bytes; }
        // New chunk
        mem_cmd_info.num_subreqs = create_mem_subreqs(mem_cmd_info.devID,
                                                      mem_cmd_info.start_addr,
                                                      rd_values_idx,
                                                      num_bytes,
                                                      mem_cmd_info.subreqs);
        res = execute_get_dev_mem_out(cdbgp, &mem_cmd_info);
        TERM_EARLY_ON_ERR(cdbgp, (res != 0), "Error received from execute_get_dev_mem_out." , res);

        mem_cmd_info.start_addr  = mem_cmd_info.start_addr  + num_bytes;
        rd_values_idx            = rd_values_idx  + num_bytes;
        mem_cmd_info.total_bytes = mem_cmd_info.total_bytes - num_bytes;
    }

    process_dump_dev_mem_out(mem_cmd_info.rw_values, rd_values_idx);

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * execute_set_dev_mem_out
 * -------------------------------------------------------------------
*/
static int
execute_set_dev_mem_out (unique_ptr<CSWPBase>& cdbgp, mem_cmd_t* m) {
    
    int ex_stat = 0, res = 0;

    // Input
    unsigned           deviceNo   = m->devID;
    unsigned           flags      = (uint32_t)m->flags;
    uint64_t           buf_idx;
    uint8_t *          buf;
    uint64_t           address;
    cswp_access_size_t accessSize;
    size_t             size;

    if (m->num_subreqs > 1) {
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_begin(1 /*abort on error*/));
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Exception caught in starting a batch.", ex_stat);
        RETURN_EARLY_ON_ERR((res != 0),     "cswp_batch_begin failed.", res);
    }

    for (uint32_t i = 0; (i < m->num_subreqs); i++) {
        address    = m->subreqs[i].saddr;
        accessSize = m->subreqs[i].acc_sz;
        size       = m->subreqs[i].size;
        buf_idx    = m->subreqs[i].buf_idx;
        buf        = &m->rw_values[buf_idx];
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_device_mem_write(deviceNo, address, size,
                                                                  accessSize, flags, buf));

        RETURN_EARLY_ON_ERR((ex_stat != 0), "Exception caught in cswp_device_mem_write.", ex_stat);
        RETURN_EARLY_ON_ERR((res != 0),     "CSWP_MEM_WRITE command failed.", res);
    }

    if (m->num_subreqs > 1) {
        uint32_t ops_completed = 0;
        ex_stat = TRAP_EX_CCU (res = cdbgp->cswp_batch_end(&ops_completed));
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Exception caught in submitting batch.", ex_stat);
        RETURN_EARLY_ON_ERR((res != 0),     "cswp_batch_end failed.", res);
        RETURN_EARLY_ON_ERR((ops_completed != m->num_subreqs), "Batch ops_completed check failed.", CCU_BATCH_OPS_ERR);
    }

    return 0;
}

/*
 * -------------------------------------------------------------------
 * set_dev_mem
 * -------------------------------------------------------------------
*/
int
set_dev_mem(int argc, char* argv[]) {

    mem_cmd_t mem_cmd_info;
    bool extracted = extract_set_dev_mem_param(argc, argv, &mem_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // In order to ensure, size to be multiple of accesssize 
    mem_cmd_info.num_subreqs = create_mem_subreqs(mem_cmd_info.devID,
                                                  mem_cmd_info.start_addr,
                                                  0, /* rw_values_idx */
                                                  mem_cmd_info.total_bytes, 
                                                  mem_cmd_info.subreqs);

    res = execute_set_dev_mem_out(cdbgp, &mem_cmd_info);
    TERM_EARLY_ON_ERR(cdbgp, (res != 0), "Error received from execute_set_dev_mem_out." , res);

    process_set_dev_mem_out();

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * fill_dev_mem
 * -------------------------------------------------------------------
*/
int
fill_dev_mem(int argc, char* argv[]) {

    mem_cmd_t mem_cmd_info;
    bool extracted = extract_fill_dev_mem_param(argc, argv, &mem_cmd_info);
    RETURN_EARLY_ON_ERR((!extracted), "cmdline arguments extraction failed!", CCU_BAD_CMD_ARG);

    unique_ptr<CSWPBase> cdbgp;
    int          res = 0;
    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    uint64_t num_bytes     = 0;
    uint64_t wr_values_idx = 0;
    // Due to limited size of CSWP RSP buffer, one CSWP_MEM_WRITE can only write 16372 bytes
    // even after employing the batch method. Therefore, CCU must send multiple of such 
    // batch/single reqs to ensure user can write upto 265kB with 1 CCU command.
    // Note that subreqs are needed so that size/req is always a multiple of access_size.
    while (mem_cmd_info.total_bytes > 0) {

        if (mem_cmd_info.total_bytes >= MEM_MAX_RW_SZ) { num_bytes  = MEM_MAX_RW_SZ;  }    
        else                                           { num_bytes  = mem_cmd_info.total_bytes; }
        // New chunk
        mem_cmd_info.num_subreqs = create_mem_subreqs(mem_cmd_info.devID,
                                                      mem_cmd_info.start_addr,
                                                      wr_values_idx,
                                                      num_bytes,
                                                      mem_cmd_info.subreqs);
        res = execute_set_dev_mem_out(cdbgp, &mem_cmd_info);
        TERM_EARLY_ON_ERR(cdbgp, (res != 0), "Error received from execute_set_dev_mem_out." , res);

        mem_cmd_info.start_addr  = mem_cmd_info.start_addr  + num_bytes;
        wr_values_idx            = wr_values_idx  + num_bytes;
        mem_cmd_info.total_bytes = mem_cmd_info.total_bytes - num_bytes;
    }

    process_set_dev_mem_out();

    res = cleanup_cdebug(cdbgp, true /* async_flush_and_discon_transport */);

    return res;
}

/*
 * -------------------------------------------------------------------
 * rd_async_msg
 * ------------------------------------------------------------------- 
*/
int
rd_async_msg(int argc, char* argv[]) {

    RETURN_EARLY_ON_ERR((argc != 2), "Exepcted 2 arguments.", CCU_BAD_CMD_ARG);
    
    unique_ptr<CSWPBase> cdbgp;
    int res = 0;

    res = setup_cdebug(cdbgp, true /* set_conn*/);
    RETURN_EARLY_ON_ERR((res != 0), "Error in setting up cdebug interface.", res);

    // Input
    size_t msg_size = MAX_ASYNC_MSG_LEN;
    // Output
    varint_t deviceNo;
    varint_t msg_level;
    char     msg[MAX_ASYNC_MSG_LEN];

    // Setup ctrl+c handler
    bool e_res = setup_user_sigint_handler();
    TERM_EARLY_ON_ERR_WO_ASYNC_FLUSH(cdbgp, (!e_res), "Error in setting custom SIGINT handler.", CCU_SIG_HANDLE_ERR);

    while (exitFlag < 1) {
        res = cdbgp->cswp_async_message(&deviceNo, &msg_level, msg, msg_size);
        if (res == CSWP_ASYNC_MSG_LOG) {
            process_async_message_out(deviceNo, msg_level, msg);
        } else if (res == CSWP_ASYNC_MSG_END) {
            sleep(3); // Sleep for 3 sec before reading again
        } else {
            TERM_EARLY_ON_ERR_WO_ASYNC_FLUSH(cdbgp, true, "CSWP_ASYNC_MESSAGE returned error.", res);
        }
    }

    e_res = restore_sigint_handler();
    TERM_EARLY_ON_ERR_WO_ASYNC_FLUSH(cdbgp, (!e_res), "Error in restoring SIGINT handler back to default.", CCU_SIG_HANDLE_ERR);

    res = cleanup_cdebug_wo_async_flush(cdbgp);

    return res;
}

/*
 * -------------------------------------------------------------------
 * cstrace_submit_client_buf
 * 
 * Submit trace data read request with given client buffer.
 * -------------------------------------------------------------------
*/
static int
cstrace_submit_client_buf(cstrace_cmd_t* c, size_t idx) {

    int tbuf_type = (idx < c->num_Dbufs) ? CSTRACE_EVENT_TYPE_DATA :
                                           CSTRACE_EVENT_TYPE_EVENT;
    int ex_stat = TRAP_EX_CCU (c->tracep->SubmitEventBuffer(c->etrID,
                                                         tbuf_type,
                                                         &c->tbufs_info[idx],
                                                         &c->tbufs_req_tokens[idx]));
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while submitting the buffer.", ex_stat);

    ++(c->num_pending_bufs);
    return 0;
}

/*
 * -------------------------------------------------------------------
 * cstrace_find_tbuf_id
 * 
 * Finds the client buffer information corresponding to given token.
 * -------------------------------------------------------------------
*/
static bool
cstrace_find_tbuf_id(cstrace_cmd_t* c, int token, size_t* id) {
    
    bool found = false;
    for (size_t i = 0; i < c->num_tbufs; ++i) {
        if (token == c->tbufs_req_tokens[i]) {
            *id   = i;
            found = true;
            break;
        }
    }
    return found;
}

/*
 * -------------------------------------------------------------------
 * cstrace_capture_data
 * -------------------------------------------------------------------
*/
static int
cstrace_capture_data(cstrace_cmd_t* c, FILE* outFile) {

    // Wait on receiving response to the USB data read reqs submitted earlier
    int token = -1;
    int ex_stat = TRAP_EX_CCU (token = c->tracep->WaitForEvent(c->etrID, WAIT_FOR_EVENT_MS));
    if (ex_stat == CSTRACE_TIMEOUT) {
        // This is expected. CSWP returns control to user rather than indefinitely waiting on data.
        return CSTRACE_TIMEOUT;
    } else {
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Error occured while waiting for server to send trace data.", ex_stat);
    }

    // Find the client buffer information corresponding to the received buffer.
    size_t tbufs_info_idx = -1;
    bool _UNUSED_PARAM_ found_buf = cstrace_find_tbuf_id(c, token, &tbufs_info_idx);
    RETURN_EARLY_ON_ERR((!found_buf), "The received buffer token is unexpected.", CCU_TBUF_NOT_FOUND_ERR);
    --(c->num_pending_bufs);

    // Process based on return buffer type
    uint8_t*         buf      = c->tbufs_info[tbufs_info_idx].pBuf;
    CSTraceEventType buf_type = c->tbufs_info[tbufs_info_idx].type;
    size_t           buf_used = c->tbufs_info[tbufs_info_idx].used;

    if (buf_type == CSTRACE_EVENT_TYPE_END_OF_DATA) {
         RETURN_EARLY_ON_ERR(true, "ETR has indicated end of trace data i.e. all data is received.", CCU_ETR_END_OF_DATA);
    } else if (buf_type == CSTRACE_EVENT_TYPE_DATA) {
        size_t bytes_written = fwrite(buf, 1, buf_used, outFile);
        RETURN_EARLY_ON_ERR((bytes_written != buf_used), "Error occurred while writing trace data to Trace file.", CCU_FILEIO_ERR);
        c->num_capture_bytes = (c->num_capture_bytes + buf_used); 
        int res = cstrace_submit_client_buf(c, tbufs_info_idx); // Reuse received buffer
        RETURN_EARLY_ON_ERR((res != 0), "Error occurred while submitting USB read request using reusable buffer.", res);
    } else {
        RETURN_EARLY_ON_ERR(true, "Received unexpected buffer type " << buf_type << " (size " << buf_used << "/" << c->tbufs_info[tbufs_info_idx].size << ")", CCU_TBUF_NOT_FOUND_ERR);
    }

    return 0;
}

/*
 * -------------------------------------------------------------------
 * capture_cstrace
 * -------------------------------------------------------------------
*/
static int
capture_cstrace(cstrace_cmd_t* c) {

    int  res           =  0; // to catch local errors
    int  ex_stat       =  0; // To catch exception from cstrace functions
    bool flush_started =  false;
    long flush_time    = -1;

    // Send request to start data streaming on target
    ex_stat = TRAP_EX_CCU (c->tracep->Start(c->etrID));
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while starting the trace.", ex_stat);

    // Create trace dump file
    FILE* outFile = fopen("cstrace.bin", "wb");
    RETURN_EARLY_ON_ERR((outFile == NULL), "Error opening strace dump file.", CCU_FILEIO_ERR);

    // Setup signal handler
    bool e_res = setup_user_sigint_handler();
    RETURN_EARLY_ON_ERR(!e_res, "Error in setting custom SIGINT handler.", CCU_SIG_HANDLE_ERR);

    // Assign tokens to all client trace data and event buffers
    // Submits USB read reqs dor all data buffers
    // Gets eventbuffer ready to receive any metadata information
    bool tbufs_sub_stat = true;
    for (size_t i = 0; i < c->num_tbufs; ++i) {
        res = cstrace_submit_client_buf(c, i);
        if (res != 0) { tbufs_sub_stat = false; break; }
    }

    long start_time = get_current_time();
    long last_current_time = start_time;
    while ((exitFlag <= 1) && (tbufs_sub_stat)) {
        if (exitFlag == 1) {
            if (!flush_started) {
                // Once user interrupts trace collection, exitFlag will be set to >= 1
                // until handler is restored but flush should only happen once. Flush
                // is used to flush the ETR buffers and request stop data streaming on target.
                PRINT_PROGRESS_STAT("Ctrl+c received. Please wait. Flushing ETR ...");
                ex_stat = TRAP_EX_CCU (c->tracep->Flush(c->etrID));
                if (ex_stat != 0) {
                    PRINT_PROGRESS_STAT("Failed to flush.");
                    // Continue until CCU_ETR_END_OF_DATA or flush timeout happens
                }
                flush_started = true;
                flush_time    = get_current_time();
            } else {
                long current_time  = get_current_time();
                long expected_time = flush_time + FLUSH_TIMEOUT;
                if ((current_time > expected_time)) {
                    PRINT_PROGRESS_STAT("Flush timed out. Started@" << flush_time/1000 << "s, Timeout@" << expected_time/1000 << "s");
                    break;
                }
            }
        }
        res = cstrace_capture_data(c, outFile);
        if ((res != 0) && (res != CSTRACE_TIMEOUT)) { break; }

        // Provide some sort of feedback on how things are going
        long current_time = get_current_time();
        if ((current_time - last_current_time) > CSTRACE_UPDATE_MS) {
            cout << "Streaming trace data, received " << c->num_capture_bytes << "B so far...\n";
            last_current_time = current_time;
        }
    }
    long stop_time = get_current_time();

    // Close trace dump file
    RETURN_EARLY_ON_ERR((fclose(outFile) != 0), "Trace file close error.", CCU_FILEIO_ERR);

    // Stop 
    ex_stat = TRAP_EX_CCU (c->tracep->Stop(c->etrID));
    RETURN_EARLY_ON_ERR((ex_stat != 0), "Error while stopping the trace.", ex_stat);

    // Discard incoming responses for pending buffers sent before user interrupt.
    while (c->num_pending_bufs > 0) {
        int _UNUSED_PARAM_ token;
        ex_stat = TRAP_EX_CCU (token = c->tracep->WaitForEvent(c->etrID, WAIT_FOR_EVENT_MS));
        RETURN_EARLY_ON_ERR((ex_stat != 0), "Error occurred while discarding pending buffer responses after user interrupt was received.", ex_stat);
        --(c->num_pending_bufs);
    }

    // Print stats
    long elapsed = stop_time-start_time;
    double speed = ((double)c->num_capture_bytes / 1024.0) / ((double)elapsed / 1000.0);
    PRINT_PROGRESS_STAT("Collected "<< c->num_capture_bytes << " bytes in " << (elapsed/1000) << "s, speed: " << speed << " kb/s");

    return 0;
}

/*
 * -------------------------------------------------------------------
 * perf_cstrace
 * 
 * This function starts and collects streaming trace until ctrl+c
 * keyboard interrupt is passed. It then dumps the trace data in
 * the cstrace.bin
 * -------------------------------------------------------------------
*/
int
perf_cstrace(int argc, char* argv[]) {

    cstrace_cmd_t cstrace_cmd_info;

    if (!extract_perf_cstrace_param(argc, argv, &cstrace_cmd_info)) { return CCU_BAD_CMD_ARG; }

    PRINT_PROGRESS_STAT("Setting up USB connection, attaching sink and allocating runtime storage...");
    int setup_staus = setup_cstrace(&cstrace_cmd_info);
    if (setup_staus != 0) { return setup_staus; }

    PRINT_PROGRESS_STAT("Starting trace data collection... press ctrl+c to stop.");
    int capture_status = capture_cstrace(&cstrace_cmd_info);

    PRINT_PROGRESS_STAT("Stopping data collection, disconnecting USB and detaching sink...");
    int cleanup_status = cleanup_cstrace(&cstrace_cmd_info);

    // We still need to disconnect on collection error, hence the late check.
    // Return with earliest occuring errorcode.
    if (capture_status != 0) { return capture_status; }
    if (cleanup_status != 0) { return cleanup_status; }

    return 0;
}
