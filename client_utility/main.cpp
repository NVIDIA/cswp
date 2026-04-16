/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 *
 * [SPEC:ARM] https://github.com/ARM-software/coresight-wire-protocol/blob/master/cswp/doc/CSWP%20Protocol%20Specification.pdf
 */

#include <iostream>
#include <string>
#include <cstring>
#include <unordered_map>
#include <functional>
#include <iomanip>

#include "ccu.h"
#include "cswp_types.h"
#include "cstrace_ex.h"
#include "cswp_client_ex.h"

using namespace std;

char *prog_name;

void
print_help(void) {
    cout << "------------------------------------------------------------------------------------------------------------------------------------------" << endl;
    cout << "Client CSWP utility (CCU)" << endl;
    cout << "Usage:"<< endl;
    cout << "------------------------------------------------------------------------------------------------------------------------------------------" << endl;
    cout << "1.  ccu help"                                                                    << endl;
    cout << "2.  ccu init_cswp"                                                               << endl;
    cout << "3.  ccu term_cswp"                                                               << endl;
    cout << "4.  ccu get_dev_info"                                                            << endl;
    cout << "5.  ccu get_sys_info"                                                            << endl;
    cout << "6.  ccu get_dev*_cap                                     (where * can be 0/1/2)" << endl;
    cout << "7.  ccu get_dev*_reg_list                                (where * can be 0)"     << endl;
    cout << "8.  ccu get_dev*_reg      <regSz> <regID>                (where * can be 0/2)"   << endl;
    cout << "9.  ccu set_dev*_reg      <regSz> <regID> <value>        (where * can be 0/2)"   << endl;
    cout << "10. ccu get_dev*_mem      <start_addr> <SZ>              (where * can be 0/1)"   << endl;
    cout << "11. ccu dump_dev*_mem     <start_addr> <SZ>              (where * can be 0/1)"   << endl;
    cout << "12. ccu set_dev*_mem      <addr> <SZ> <value>            (where * can be 0/1)"   << endl;
    cout << "13. ccu fill_dev*_mem     <start_addr> <bin file>        (where * can be 0/1)"   << endl;
    cout << "14. ccu rd_async_msg"                                                            << endl;
    cout << "15. ccu cstrace           <etr_id>"                                              << endl;
    cout << "-------------------------------------------------------------------------------------------------------------------------" << endl;
}

// Unordered map of function name string and function pointers
std::unordered_map <std::string, ccufun_ptr> funcMap = {
        {"init_cswp"        , init_cswp},
        {"term_cswp"        , term_cswp},
        {"get_dev_info"     , get_dev_info},
        {"get_dev0_cap"     , get_dev_cap},
        {"get_dev1_cap"     , get_dev_cap},
        {"get_dev2_cap"     , get_dev_cap},
        {"get_sys_info"     , get_sys_info},
        {"get_dev0_reg_list", get_reg_list},
        {"get_dev0_reg"     , get_dev_reg},
        {"set_dev0_reg"     , set_dev_reg},
        {"get_dev2_reg"     , get_dev_reg},
        {"set_dev2_reg"     , set_dev_reg},
        {"get_dev0_mem"     , get_dev_mem},
        {"get_dev1_mem"     , get_dev_mem},
        {"set_dev0_mem"     , set_dev_mem},
        {"set_dev1_mem"     , set_dev_mem},
        {"dump_dev0_mem"    , dump_dev_mem},
        {"dump_dev1_mem"    , dump_dev_mem},
        {"fill_dev0_mem"    , fill_dev_mem},
        {"fill_dev1_mem"    , fill_dev_mem},
        {"rd_async_msg"     , rd_async_msg},
        {"cstrace"          , perf_cstrace}
};

// Unordered map of all errcodes and their names
std::unordered_map <int, std::string> errMap = {
        {CSWP_SUCCESS, "cswp_error:CSWP_SUCCESS"},
        {CSWP_FAILED, "cswp_error:CSWP_FAILED"},
        {CSWP_CANCELLED, "cswp_error:CSWP_CANCELLED"},
        {CSWP_NOT_INITIALIZED, "cswp_error:CSWP_NOT_INITIALIZED"},
        {CSWP_ASYNC_MSG_LOG, "cswp_async:CSWP_ASYNC_MSG_LOG"},
        {CSWP_ASYNC_MSG_END, "cswp_async:CSWP_ASYNC_MSG_END"},
        {CSWP_NVSEC_TOKEN_REQ_ERR, "cswp_error:CSWP_NVSEC_TOKEN_REQ_ERR"},
        {CSWP_NVSEC_SIGNED_TOKEN_ERR, "cswp_error:CSWP_NVSEC_SIGNED_TOKEN_ERR"},
        {CSWP_NVSEC_SIGNATURE_ERR, "cswp_error:CSWP_NVSEC_SIGNATURE_ERR"},
        {CSWP_NVSEC_PUBLIC_KEY_ERR, "cswp_error:CSWP_NVSEC_PUBLIC_KEY_ERR"},
        {CSWP_BUFFER_FULL, "cswp_error:CSWP_BUFFER_FULL"},
        {CSWP_BUFFER_EMPTY, "cswp_error:CSWP_BUFFER_EMPTY"},
        {CSWP_OUTPUT_BUFFER_OVERFLOW, "cswp_error:CSWP_OUTPUT_BUFFER_OVERFLOW"},
        {CSWP_UNEXPECTED_RESPONSE, "cswp_error:CSWP_UNEXPECTED_RESPONSE"},
        {CSWP_RESPONSE_SIZE_ERR, "cswp_error:CSWP_RESPONSE_SIZE_ERR"}, 
        {CSWP_COMMS, "cswp_error:CSWP_COMMS"},
        {CSWP_INCOMPATIBLE, "cswp_error:CSWP_INCOMPATIBLE"},
        {CSWP_TIMEOUT, "cswp_error:CSWP_TIMEOUT"},
        {CSWP_UNSUPPORTED, "cswp_error:CSWP_UNSUPPORTED"},
        {CSWP_DEVICE_UNSUPPORTED, "cswp_error:CSWP_DEVICE_UNSUPPORTED"},
        {CSWP_INVALID_DEVICE, "cswp_error:CSWP_INVALID_DEVICE"},
        {CSWP_BAD_ARGS, "cswp_error:CSWP_BAD_ARGS"},
        {CSWP_NOT_PERMITTED, "cswp_error:CSWP_NOT_PERMITTED"},
        {CSWP_REG_FAILED, "cswp_error:CSWP_REG_FAILED"},
        {CSWP_REG_PARTIAL, "cswp_error:CSWP_REG_PARTIAL"},
        {CSWP_MEM_FAILED, "cswp_error:CSWP_MEM_FAILED"},
        {CSWP_MEM_INVALID_ADDRESS, "cswp_error:CSWP_MEM_INVALID_ADDRESS"},
        {CSWP_MEM_BAD_ACCESS_SIZE, "cswp_error:CSWP_MEM_BAD_ACCESS_SIZE"},
        {CSWP_MEM_POLL_NO_MATCH, "cswp_error:CSWP_MEM_POLL_NO_MATCH"},
        {CDEBUG_INVALID_TARGET, "cswp_error:CDEBUG_INVALID_TARGET"},
        {CDEBUG_INVALID_CONFIG, "cswp_error:CDEBUG_INVALID_CONFIG"},
        {CSTRACE_NO_CONNECTION, "cswp_error:CSTRACE_NO_CONNECTION"},
        {CSTRACE_NO_EVENT, "cswp_error:CSTRACE_NO_EVENT"},
        {CSTRACE_COMMSERROR, "cswp_error:CSTRACE_COMMSERROR"},
        {CSTRACE_TIMEOUT, "cswp_error:CSTRACE_TIMEOUT"},
        {CSTRACE_NO_DEVICE, "cswp_error:CSTRACE_NO_DEVICE"},
        {CSTRACE_INVALID_SINK, "cswp_error:CSTRACE_INVALID_SINK"},
        {CSTRACE_SINK_NOT_ATTACHED, "cswp_error:CSTRACE_SINK_NOT_ATTACHED"},
        {CSTRACE_SINK_NOT_STARTED, "cswp_error:CSTRACE_SINK_NOT_STARTED"},
        {CSTRACE_CONNECT_FAILED, "cswp_error:CSTRACE_CONNECT_FAILED"},
        {CSTRACE_SINK_START_FAILED, "cswp_error:CSTRACE_SINK_START_FAILED"},
        {CSTRACE_SINK_STOP_FAILED, "cswp_error:CSTRACE_SINK_STOP_FAILED"},
        {CSTRACE_DATA_OVERFLOW, "cswp_error:CSTRACE_DATA_OVERFLOW"},
        {CSTRACE_DATA_INTEGRITY, "cswp_error:CSTRACE_DATA_INTEGRITY"},
        {CSTRACE_AUX_PROBE_PROG, "cswp_error:CSTRACE_AUX_PROBE_PROG"},
        {CSTRACE_SET_MODE_FAILED, "cswp_error:CSTRACE_SET_MODE_FAILED"},
        {CSTRACE_INVALID_EVENT_TYPE, "cswp_error:CSTRACE_INVALID_EVENT_TYPE"},
        {CSTRACE_INVALID_TARGET, "cswp_error:CSTRACE_INVALID_TARGET"},
        {CSTRACE_INVALID_CONFIG, "cswp_error:CSTRACE_INVALID_CONFIG"},
        {CCU_CMD_NOT_SUPPORTED, "ccu_error:CCU_CMD_NOT_SUPPORTED"},
        {CCU_USB_RUNTIME_EXCEPTION, "ccu_error:CCU_USB_RUNTIME_EXCEPTION"},
        {CCU_UNKNOWN_EXCEPTION, "ccu_error:CCU_UNKNOWN_EXCEPTION"},
        {CCU_CFG_FILE_ERR, "ccu_error:CCU_CFG_FILE_ERR"},
        {CCU_BAD_CMD_ARG, "ccu_error:CCU_BAD_CMD_ARG"},
        {CCU_FILEIO_ERR, "ccu_error:CCU_FILEIO_ERR"},
        {CCU_MALLOC_ERR, "ccu_error:CCU_MALLOC_ERR"},
        {CCU_SIG_HANDLE_ERR, "ccu_error:CCU_SIG_HANDLE_ERR"},
        {CCU_TBUF_NOT_FOUND_ERR, "ccu_error:CCU_TBUF_NOT_FOUND_ERR"},
        {CCU_BATCH_OPS_ERR, "ccu_error:CCU_BATCH_OPS_ERR"},
        {CCU_ETR_END_OF_DATA, "ccu_error:CCU_ETR_END_OF_DATA"}
};

void
print_err(int errcode) {
    string errname;
    auto it = errMap.find(errcode);
    if (it == errMap.end()) { errname = "UNKNOWN_ERROR."; }
    else                    { errname = it->second; }

    cout << "[ccu_main] " << errname << "(errcode:0x" << hex << setw(4) << setfill('0') << errcode << ")" << endl;
}

extern "C" int
main(int argc, char* argv[]) {

    prog_name = argv[0];
    if (argc < 2) { print_err(CCU_BAD_CMD_ARG); return -1; }

    if ((strcmp(argv[1], "help") == 0) || (strcmp(argv[1], "-help") == 0)
                || (strcmp(argv[1], "--help") == 0)) {
        if (argc != 2) { print_err(CCU_BAD_CMD_ARG); return -1; }
        print_help();
        return 0;
    }

    auto it = funcMap.find(argv[1]);
    if (it == funcMap.end()) { print_err(CCU_CMD_NOT_SUPPORTED); return -1; }
    else {
       int res = it->second(argc, argv);
       if (res != CSWP_SUCCESS) { print_err(res); return -1; }
    }

    return 0;
}
