/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.txt for the full license.
 */

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <iomanip>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fstream>
#include <iterator>
#include <libgen.h>

// Error codes
#define CSWP_SUCCESS                0x0000
#define CSWP_FAILED                 0x0001
#define CSWP_CANCELLED              0x0002
#define CSWP_NOT_INITIALIZED        0x0003
#define CSWP_BUFFER_FULL            0x0010
#define CSWP_BUFFER_EMPTY           0x0011
#define CSWP_OUTPUT_BUFFER_OVERFLOW 0x0012
#define CSWP_COMMS                  0x0020
#define CSWP_INCOMPATIBLE           0x0021
#define CSWP_TIMEOUT                0x0022
#define CSWP_UNSUPPORTED            0x0023
#define CSWP_DEVICE_UNSUPPORTED     0x0024
#define CSWP_INVALID_DEVICE         0x0025
#define CSWP_BAD_ARGS               0x0026
#define CSWP_NOT_PERMITTED          0x0028
#define CSWP_REG_FAILED             0x0200
#define CSWP_REG_PARTIAL            0x0201
#define CSWP_MEM_FAILED             0x0300
#define CSWP_MEM_INVALID_ADDRESS    0x0301
#define CSWP_MEM_BAD_ACCESS_SIZE    0x0302
#define CSWP_MEM_POLL_NO_MATCH      0x0303

// Messages
#define CSWP_INIT                       0x00000001
#define CSWP_TERM                       0x00000002
#define CSWP_CLIENT_INFO                0x00000005
#define CSWP_SET_DEVICES                0x00000010
#define CSWP_GET_DEVICES                0x00000011
#define CSWP_GET_SYSTEM_DESCRIPTION     0x00000012
#define CSWP_DEVICE_OPEN                0x00000100
#define CSWP_DEVICE_CLOSE               0x00000101
#define CSWP_SET_CONFIG                 0x00000102
#define CSWP_GET_CONFIG                 0x00000103
#define CSWP_GET_DEVICE_CAPABILITIES    0x00000104
#define CSWP_REG_LIST                   0x00000200
#define CSWP_REG_READ                   0x00000201
#define CSWP_REG_WRITE                  0x00000202
#define CSWP_MEM_READ                   0x00000300
#define CSWP_MEM_WRITE                  0x00000301
#define CSWP_MEM_POLL                   0x00000302
#define CSWP_ASYNC_MESSAGE              0x00001000

using namespace std;

typedef struct {
    int cmd_enc;
    int apnum;
    const uint8_t *data;
} cmdinfo;

typedef int (*ccufun_ptr) (cmdinfo);

uint8_t *resp_buffer = nullptr;
size_t resp_buffer_size;
size_t resp_buffer_nresp;

char * prog_name;

void
print_msg(string msg) {
    cout << msg << endl;
}

void printdata(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        cout << hex
                  << setw(2) << setfill('0')
                  << (static_cast<unsigned int>(static_cast<unsigned char>(data[i]))) << " ";
    }
    cout << dec << endl;  // switch back to decimal
}

/*
 * ---------------------------------------------------------------------------------------------------
 * parse_transport_cfg_file
 * ---------------------------------------------------------------------------------------------------
*/
int
parse_transport_cfg_file(string& ipaddr, int* port) {

    map<string, string> cfg_map;

    ifstream file("cdebug_trnsprt_cfg.txt");
#ifdef CONFIG_FILES_PATH
    if (!file.is_open()) {
        char *tmp = strdup(prog_name);
        char *prog_dir_name = dirname(tmp);
        char *cfg_file_name = (char *)malloc(strlen(prog_dir_name) + strlen("/" CONFIG_FILES_PATH "/cdebug_trnsprt_cfg.txt") + 1);
        strcpy(cfg_file_name, prog_dir_name);
        strcat(cfg_file_name, "/" CONFIG_FILES_PATH "/cdebug_trnsprt_cfg.txt");
        file = ifstream(cfg_file_name);
        free(cfg_file_name);
        free(tmp);
        if (!file.is_open()) {
            print_msg("- Server cannot find the cdebug_trnsprt_cfg.txt for read.");
            return -1;
        }
    }
#else
    if (!file.is_open()) {
        print_msg("- Server cannot find the cdebug_trnsprt_cfg.txt for read.");
        return -1;
    }
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

    if(cfg_map.empty() || (cfg_map.count("type") <= 0)) {
        print_msg("- Please make sure to set appropriate transport config data in file cdebug_trnsprt_cfg.txt in this directory.");
        return -1;
    }

    // Extract cfguration data
    if (cfg_map["type"] != "tcp") {
        print_msg("- Dummy server expect transport type to be tcp in cdebug_trnsprt_cfg.txt.");
        return -1;
    }

    ipaddr = cfg_map["ipaddr"];
    *port  = stoi(cfg_map["port"]);

    return 0;
}

//----------------------------------------------
// start_server
//----------------------------------------------
int
start_server (int* server_fd, int* client_socket) {

    string ipaddr;
    int port;

    int status = parse_transport_cfg_file(ipaddr, &port);
    if (status != 0) {
        print_msg("- Error reading transport cfg.");
        return -1;
    }

    // Create socket
    *server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd < 0) {
        print_msg("- Error creating socket");
        return -1;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (::setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        print_msg("- Error setting socket options");
        return -2;
    }

    // Configure server address
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ipaddr.c_str());
    address.sin_port = htons(port);

    // Bind socket to port
    if (::bind(*server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error binding socket to port");
        return -3;
    }

    // Listen for connections
    if (::listen(*server_fd, 3) < 0) {
        print_msg("- Error listening for connections");
        return -4;
    }
    //cout << "- Server started on " << ipaddr << ". Waiting on client to connect to port " << port << "...";

    // Accept incoming connection
    struct sockaddr_in client_address;
    socklen_t addrlen = sizeof(client_address);
    *client_socket = ::accept(*server_fd, (struct sockaddr *)&client_address, &addrlen);
    if (*client_socket < 0) {
        print_msg("- Error accepting connection");
        return -5;
    }
    //print_msg("- Client connection acccepted!");

    return 0;
}

//----------------------------------------------
// stop_server
//----------------------------------------------
void
stop_server(int server_fd, int client_socket) {
    ::close(client_socket);
    ::close(server_fd);
    //print_msg("- Server terminated.");
}

//----------------------------------------------
// encode_varint
//----------------------------------------------
vector<uint8_t> encode_varint(uint64_t value) {
    vector<uint8_t> result;
    while (value >= 0x80) {
        result.push_back((uint8_t)(value | 0x80));
        value >>= 7;
    }
    result.push_back((uint8_t)value);
    return result;
}

//----------------------------------------------
// encode_uint32
//----------------------------------------------
vector<uint8_t> encode_uint32(uint32_t value) {
    vector<uint8_t> result;
    result.push_back((uint8_t)value);
    result.push_back((uint8_t)(value>>8));
    result.push_back((uint8_t)(value>>16));
    result.push_back((uint8_t)(value>>24));
    return result;
}

//----------------------------------------------
// str_to_utf8_len_bytes
//----------------------------------------------
vector<uint8_t> str_to_utf8_len_bytes(const string& str) {
    vector<uint8_t> result;
    // Assuming a simple UTF-8 length prefix encoding
    int length = str.length();
    vector<uint8_t> length_bytes = encode_varint(length);
    result.insert(result.end(), length_bytes.begin(), length_bytes.end());
    result.insert(result.end(), str.begin(), str.end());
    return result;
}

//----------------------------------------------
// msg_len_bytes
//----------------------------------------------
vector<uint8_t> msg_len_bytes(uint32_t rsp_len) {
    if (rsp_len > 0xFFFFFFFF) {
        cout << "- ERROR: Request length " << rsp_len 
                  << " is greater than the allowed size 0xFFFFFFFF." << endl;
        exit(1);
    }
    
    vector<uint8_t> msg_len_bytes(4);
    msg_len_bytes[0] = rsp_len & 0xFF;
    msg_len_bytes[1] = (rsp_len >> 8) & 0xFF;
    msg_len_bytes[2] = (rsp_len >> 16) & 0xFF;
    msg_len_bytes[3] = (rsp_len >> 24) & 0xFF;
    
    return msg_len_bytes;
}

//----------------------------------------------
// respond_send
//----------------------------------------------
void
response_send(int socket_id) {
    size_t offset = 0;
    size_t send_buffer_size;
    
    if (resp_buffer == nullptr) {
        cout << "ERROR: respond_send called but no data to send.";
        return;
    }
    vector<uint8_t> num_sub_responses = encode_varint(resp_buffer_nresp);
    send_buffer_size = resp_buffer_size + 4 + num_sub_responses.size();
    vector<uint8_t> message_length = encode_uint32(send_buffer_size);
    
    uint8_t* packet = new uint8_t[send_buffer_size];
    memcpy(packet+offset, message_length.data(), message_length.size());
    offset += message_length.size();
    memcpy(packet+offset, num_sub_responses.data(), num_sub_responses.size());
    offset += num_sub_responses.size();
    memcpy(packet+offset, resp_buffer, resp_buffer_size);
    ::send(socket_id, packet, send_buffer_size, 0);
    delete[] packet;
    free(resp_buffer);
    resp_buffer = nullptr;
}

//----------------------------------------------
// respond_add
//----------------------------------------------
void
response_add(const uint8_t *resp, size_t resp_size) {
    if (resp_buffer == nullptr) {
        resp_buffer = (uint8_t *)malloc(resp_size);
        resp_buffer_size = 0;
        resp_buffer_nresp = 0;
    } else {
        resp_buffer = (uint8_t *)realloc(resp_buffer, resp_buffer_size + resp_size);
    }
    memcpy(resp_buffer+resp_buffer_size, resp, resp_size);
    resp_buffer_size += resp_size;
    resp_buffer_nresp++;
}

//----------------------------------------------
// respond_err
//----------------------------------------------
void
response_err(cmdinfo info, int errcode) {
    cout << "Command " << info.cmd_enc << " failed with errcode: 0x" << hex << setw(4) << setfill('0') << errcode << dec << endl;

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    vector<uint8_t> err_rsp     = encode_varint(errcode);
    int rsp_len =  (rsp_type.size()         +
                    err_rsp.size()
                    );
    vector<uint8_t> msg_len = msg_len_bytes(rsp_len);
    
    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    memcpy(rsp_bytes + offset, err_rsp.data(), err_rsp.size());
    offset += err_rsp.size();
    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
}

//----------------------------------------------
// process_incoming_varint
// Process data as a varint and store the value in value, return the number of
// bytes consumed.
//----------------------------------------------
int
process_incoming_varint(const uint8_t *data, uint64_t *value) {
    int index;
    
    if (value != nullptr)
        *value = 0;
    for(index = 0; ; index++) {
        if (value != nullptr)
            *value += (data[index] & 0x7F) << (index*7);
        if ((data[index] & 0x80) == 0)
            break;
    }
    return index+1;
}

//----------------------------------------------
// process_incoming_string
// Process data as a string, allocate space for string and return the number of
// bytes consumed. Caller is responsible for freeing *str_p with free.
//----------------------------------------------
int
process_incoming_string(const uint8_t *data, char **str_p) {
    uint64_t str_size;
    int offset = process_incoming_varint(data, &str_size);

    if (str_p == nullptr)
        return offset+str_size;
    *str_p = (char *)malloc(str_size+1);
    for(size_t index = 0; index < str_size; index++, offset++) {
        (*str_p)[index] = data[offset];
    }
    (*str_p)[str_size] = 0;
    return offset;
}

int
process_incoming_uint32(const uint8_t *data, uint32_t *value) {
    int count = 0;
    if (value == nullptr)
        return sizeof(uint32_t);
    *value = (uint32_t)data[count++];
    *value |= (uint32_t)data[count++] << 8;
    *value |= (uint32_t)data[count++] << 16;
    *value |= (uint32_t)data[count++] << 24;
    return count;
}

int
process_incoming_uint64(const uint8_t *data, uint64_t *value) {
    int count = 0;
    if (value == nullptr)
        return sizeof(uint64_t);
    *value = (uint64_t)data[count++];
    *value |= (uint64_t)data[count++] << 8;
    *value |= (uint64_t)data[count++] << 16;
    *value |= (uint64_t)data[count++] << 24;
    *value |= (uint64_t)data[count++] << 32;
    *value |= (uint64_t)data[count++] << 40;
    *value |= (uint64_t)data[count++] << 48;
    *value |= (uint64_t)data[count++] << 56;
    return count;
}

//----------------------------------------------
// process_incoming_data
// Return an array of cmd_info, caller is responsible for freeing the array
// Return value is the number of elements in the array if positive or an error
// if negative.
//----------------------------------------------
int
process_incoming_data(const uint8_t* data,  cmdinfo** info_array_p,
                        uint8_t *error_mode_p) {
    uint32_t message_length;
    uint64_t num_sub_requests;
    size_t offset = 0;
    static int batch = 0;

    batch++;
    offset += process_incoming_uint32(data+offset, &message_length);
    offset += process_incoming_varint(data+offset, &num_sub_requests);
    *error_mode_p = data[offset++];
    *info_array_p = (cmdinfo *)calloc(num_sub_requests, sizeof(cmdinfo));
    for(uint64_t cmd = 0; cmd < num_sub_requests; cmd++) {
        cmdinfo *info = &((*info_array_p)[cmd]);
        uint64_t enc;

        offset += process_incoming_varint(data+offset, &enc);
        info->cmd_enc = enc;
        info->data = data+offset;
        if (enc == CSWP_INIT) {
            uint64_t protocol_version;
            char *client_id;
            info->apnum = -1;
            offset += process_incoming_varint(data+offset, &protocol_version);
            offset += process_incoming_string(data+offset, &client_id);
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_INIT"
                    << " protocol_version=0x" << std::hex << protocol_version
                    << " client_id='" << std::dec << client_id
                    << "'\n";
            free(client_id);
        } else if (enc == CSWP_TERM) {
            info->apnum = -1;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_TERM\n";
        } else if (enc == CSWP_CLIENT_INFO) {
            char *message;
            offset += process_incoming_string(data+offset, &message);
            info->apnum = -1;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_CLIENT_INFO"
                    << " message='" << message
                    << "'\n";
            free(message);
        } else if (enc == CSWP_SET_DEVICES) {
            uint64_t ndevs;
            offset += process_incoming_varint(data+offset, &ndevs);
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_SET_DEVICES"
                    << " device_count=" << ndevs << "\n";
            for(uint64_t i = 0; i < ndevs; i++) {
                char *device_name, *device_type;
                offset += process_incoming_string(data+offset, &device_name);
                offset += process_incoming_string(data+offset, &device_type);
                cout << "\tdev" << i
                        << " device_name='" << device_name
                        << "' device_type='" << device_type
                        << "'\n";
                free(device_name);
                free(device_type);
            }
            info->apnum = -1;
        } else if (enc == CSWP_GET_DEVICES) {
            info->apnum = -1;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_GET_DEVICES\n";
        } else if (enc == CSWP_GET_SYSTEM_DESCRIPTION) {
            info->apnum = -1;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_GET_SYSTEM_DESCRIPTION\n";
        } else if (enc == CSWP_DEVICE_OPEN) {
            uint64_t dev_id;
            offset += process_incoming_varint(data+offset, &dev_id);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_DEVICE_OPEN"
                    << " dev_id=" << dev_id << "\n";
        } else if (enc == CSWP_DEVICE_CLOSE) {
            uint64_t dev_id;
            offset += process_incoming_varint(data+offset, &dev_id);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_DEVICE_CLOSE"
                    << " dev_id=" << dev_id << "\n";
        } else if (enc == CSWP_SET_CONFIG) {
            uint64_t dev_id;
            char *name, *value;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_string(data+offset, &name);
            offset += process_incoming_string(data+offset, &value);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_SET_CONFIG"
                    << " dev_id=" << dev_id
                    << " name='" << name
                    << "' value='" << dev_id
                    << "'\n";
            free(name);
            free(value);
        } else if (enc == CSWP_GET_CONFIG) {
            uint64_t dev_id;
            char *name;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_string(data+offset, nullptr);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_GET_CONFIG"
                    << " dev_id=" << dev_id
                    << " name='" << name
                    << "'\n";
            free(name);
        } else if (enc == CSWP_GET_DEVICE_CAPABILITIES) {
            uint64_t dev_id;
            offset += process_incoming_varint(data+offset, &dev_id);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_GET_DEVICE_CAPABILITIES"
                    << " dev_id=" << dev_id
                    << "'\n";
        } else if (enc == CSWP_REG_LIST) {
            uint64_t dev_id;
            offset += process_incoming_varint(data+offset, &dev_id);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_REG_LIST"
                    << " dev_id=" << dev_id << "\n";
        } else if (enc == CSWP_REG_READ) {
            uint64_t dev_id, count;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_varint(data+offset, &count);
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_REG_READ"
                    << " dev_id=" << dev_id
                    << " count=" << count
                    << "\n";
            for(uint64_t i = 0; i < count; i++) {
                uint64_t register_id;
                offset += process_incoming_varint(data+offset, &register_id);
                cout << "\t" << i << ": register_id=0x" << std::hex << register_id
                    << std::dec << "\n";
            }
            info->apnum = dev_id;
        } else if (enc == CSWP_REG_WRITE) {
            uint64_t dev_id, count;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_varint(data+offset, &count);
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_REG_WRITE"
                    << " dev_id=" << dev_id
                    << " count=" << count
                    << "\n";
            for(uint64_t i = 0; i < count; i++) {
                uint64_t register_id;
                uint32_t register_data;
                offset += process_incoming_varint(data+offset, &register_id);
                offset += process_incoming_uint32(data+offset, &register_data);
                cout << "\t" << i << ": register_id=0x" << std::hex << register_id
                        << ": register_data=0x" << std::hex << register_data
                        << std::dec << "\n";
            }
            info->apnum = dev_id;
        } else if (enc == CSWP_MEM_READ) {
            uint64_t dev_id, address, size, access_size, flags;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_uint64(data+offset, &address);
            offset += process_incoming_varint(data+offset, &size);
            offset += process_incoming_varint(data+offset, &access_size);
            offset += process_incoming_varint(data+offset, &flags);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_MEM_READ"
                    << " dev_id=" << dev_id
                    << " address=0x" << std::hex << address
                    << " size=" << std::dec << size
                    << " access_size=" << access_size
                    << " flags=0x" << std::hex << flags
                    << std::dec << "\n";
        } else if (enc == CSWP_MEM_WRITE) {
            uint64_t dev_id, address, size, access_size, flags;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_uint64(data+offset, &address);
            offset += process_incoming_varint(data+offset, &size);
            offset += process_incoming_varint(data+offset, &access_size);
            offset += process_incoming_varint(data+offset, &flags);
            offset += size * sizeof(uint8_t);
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_MEM_WRITE"
                    << " dev_id=" << dev_id
                    << " address=0x" << std::hex << address
                    << " size=" << std::dec << size
                    << " access_size=" << access_size
                    << " flags=0x" << std::hex << flags
                    << std::dec << "\n";
        } else if (enc == CSWP_MEM_POLL) {
            uint64_t dev_id, address, size, access_size, flags, interval;
            offset += process_incoming_varint(data+offset, &dev_id);
            offset += process_incoming_uint64(data+offset, &address);
            offset += process_incoming_varint(data+offset, &size);
            offset += process_incoming_varint(data+offset, &access_size);
            offset += process_incoming_varint(data+offset, &flags);
            offset += process_incoming_varint(data+offset, &interval);
            offset += size * sizeof(uint8_t) * 2;
            info->apnum = dev_id;
            cout << "Cmd" << batch << "-" << cmd << ": CSWP_MEM_POLL"
                    << " dev_id=" << dev_id
                    << " address=0x" << std::hex << address
                    << " size=" << std::dec << size
                    << " access_size=" << access_size
                    << " flags=0x" << std::hex << flags
                    << " interval=" << std::dec << interval
                    << "\n";
        } else {
            cout << "ERROR: command " << enc << " is unknown, unable to process remaining " << message_length-offset << " bytes!" << endl;
            return cmd;
        }
    }
    return num_sub_requests;
}

//----------------------------------------------
// fudge_init_cswp
//----------------------------------------------
int
fudge_init_cswp(cmdinfo info) {

    int OPEN_PROTV  = 0x100;

    vector<uint8_t> rsp_type              = encode_varint(info.cmd_enc);
    uint8_t         errcode               = 0x0;
    vector<uint8_t> serverProtocolVersion = encode_varint(OPEN_PROTV);
    vector<uint8_t> serverID              = str_to_utf8_len_bytes("Vera CSWP Server");
    vector<uint8_t> serverVersion         = encode_varint(0x100);

    int rsp_len = (rsp_type.size() + 
                   1 /* size of errcode  */ + 
                   serverProtocolVersion.size() + 
                   serverID.size() +
                   serverVersion.size());

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, serverProtocolVersion.data(), serverProtocolVersion.size());
    offset += serverProtocolVersion.size();
    memcpy(rsp_bytes + offset, serverID.data(), serverID.size());
    offset += serverID.size();
    memcpy(rsp_bytes + offset, serverVersion.data(), serverVersion.size());

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_term_cswp
//----------------------------------------------
int
fudge_term_cswp(cmdinfo info) {

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */);

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_get_devices
//----------------------------------------------
int
fudge_get_devices(cmdinfo info) {

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;
    uint8_t         deviceCount = 0x3;
    vector<uint8_t> dev0        = str_to_utf8_len_bytes("APB Bridge");
    vector<uint8_t> dev0_type   = str_to_utf8_len_bytes("memory");
    vector<uint8_t> dev1        = str_to_utf8_len_bytes("System Memory Bridge");
    vector<uint8_t> dev1_type   = str_to_utf8_len_bytes("memory");
    vector<uint8_t> dev2        = str_to_utf8_len_bytes("CTLP Bridge");
    vector<uint8_t> dev2_type   = str_to_utf8_len_bytes("nvctlp");

    dev0.insert(dev0.end(), dev0_type.begin(), dev0_type.end());
    dev1.insert(dev1.end(), dev1_type.begin(), dev1_type.end());
    dev2.insert(dev2.end(), dev2_type.begin(), dev2_type.end());


    int rsp_len = (rsp_type.size() + 
                   1 /* size of errcode   */ + 
                   1 /* size of dev count */ + 
                   dev0.size() + 
                   dev1.size() +
                   dev2.size());

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    rsp_bytes[offset] = deviceCount;
    offset++;
    memcpy(rsp_bytes + offset, dev0.data(), dev0.size());
    offset += dev0.size();
    memcpy(rsp_bytes + offset, dev1.data(), dev1.size());
    offset += dev1.size();
    memcpy(rsp_bytes + offset, dev2.data(), dev2.size());

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_get_sys_info
//----------------------------------------------
int
fudge_get_sys_info(cmdinfo info) {

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;
    uint8_t         format      = 0x0; // 0:SDF, 1:SDF gzipped

    std::ifstream f("vera_sil.sdf", std::ios::binary);
    std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    f.close();
    vector<uint8_t> file_size   = encode_varint(file_data.size());

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   1 /* size of format    */ +
                   file_size.size()          +
                   file_data.size());

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    rsp_bytes[offset] = format;
    offset++;
    memcpy(rsp_bytes + offset, file_size.data(), file_size.size());
    offset += file_size.size();
    memcpy(rsp_bytes + offset, file_data.data(), file_data.size());

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_open_dev
//----------------------------------------------
int
fudge_open_dev(cmdinfo info) {

    // Note that this response matches what ccu expects in
    // init_cswp function which is a batch cmd to open all
    // devices at once.

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;
    vector<uint8_t> deviceInfo0 = str_to_utf8_len_bytes("APB Bridge");
    vector<uint8_t> deviceInfo1 = str_to_utf8_len_bytes("System Memory Bridge");
    vector<uint8_t> deviceInfo2 = str_to_utf8_len_bytes("CTLP Bridge");

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */ + 
                   deviceInfo0.size()        +
                   rsp_type.size()           + 
                   1 /* size of errcode   */ + 
                   deviceInfo1.size()        +
                   rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   deviceInfo2.size());

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, deviceInfo0.data(), deviceInfo0.size());
    offset += deviceInfo0.size();
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, deviceInfo1.data(), deviceInfo1.size());
    offset += deviceInfo1.size();
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, deviceInfo2.data(), deviceInfo2.size());

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_close_dev
//----------------------------------------------
int
fudge_close_dev(cmdinfo info) {

    // Note that this response matches what ccu expects in
    // init_cswp function which is a batch cmd to close all
    // devices at once.
    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   rsp_type.size()           + 
                   1 /* size of errcode   */);

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_get_dev_cap
//----------------------------------------------
int
fudge_get_dev_cap(cmdinfo info) {

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;

    int caps;
    int caps_data = 0x0; // Max regs currently set to 0

    if (info.apnum == 0) {
        caps = 0x203; // REG-MEM (CSWP_CAP_REG, CSWP_CAP_MEM, CSWP_CAP_MEM_POLL)
    } else if (info.apnum == 1) {
        caps = 0x202; // MEM only (CSWP_CAP_MEM, CSWP_CAP_MEM_POLL)
    } else if (info.apnum == 2) {
        caps = 0x01;   // REG only (CSWP_CAP_REG)
    } else {
        cout << "- ERROR: Invalid device_id " << info.apnum << "." << endl;
        return -1;
    }

    vector<uint8_t> capabilities = encode_varint(caps);
    vector<uint8_t> capabilities_data;
    if (info.apnum != 1) {
        capabilities_data = encode_varint(caps_data);
    }

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   capabilities.size()       +
                   capabilities_data.size());

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, capabilities.data(), capabilities.size());
    offset += capabilities.size();
    memcpy(rsp_bytes + offset, capabilities_data.data(), capabilities_data.size());
    offset += capabilities_data.size();

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_get_reg_list
//----------------------------------------------
int
fudge_get_reg_list(cmdinfo info) {

    if ((info.apnum == 1) || (info.apnum == 2)) {
        // No register
        vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
        uint8_t         errcode     = 0x0;
        uint8_t         num_regs    = 0x0; 
        int rsp_len =  (rsp_type.size()           + 
                        1 /* size of errcode   */ +
                        1 /* num_regs          */
                        );

        uint8_t* rsp_bytes = new uint8_t[rsp_len];
        int offset = 0;
        memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
        offset += rsp_type.size();
        rsp_bytes[offset] = errcode;
        offset++;
        rsp_bytes[offset] = num_regs;
        offset++;
        response_add(rsp_bytes, rsp_len);
        delete[] rsp_bytes;
        return 0;
    } else {
        return CSWP_DEVICE_UNSUPPORTED;
    }

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;
    uint8_t         num_regs    = 0x4; 
    uint8_t         regSize    =  0x1; // all 4 are 32 bit.

    vector<uint8_t> regID0          = encode_varint(0xD00);
    vector<uint8_t> regName0        = str_to_utf8_len_bytes("CSW");
    vector<uint8_t> regDescription0 = str_to_utf8_len_bytes("Fake CSW register, used to support ARM-DS.");

    vector<uint8_t> regID1          = encode_varint(0xDF8);
    vector<uint8_t> regName1        = str_to_utf8_len_bytes("BASE");
    vector<uint8_t> regDescription1 = str_to_utf8_len_bytes("ROM table entry address.");

    vector<uint8_t> regID2          = encode_varint(0xDFC);
    vector<uint8_t> regName2        = str_to_utf8_len_bytes("IDR");
    vector<uint8_t> regDescription2 = str_to_utf8_len_bytes("Fake IDR register, used to support ARM-DS.");

    vector<uint8_t> regID3          = encode_varint(0x4e560000);
    vector<uint8_t> regName3        = str_to_utf8_len_bytes("ASYNC_MSG");
    vector<uint8_t> regDescription3 = str_to_utf8_len_bytes("Configuration register for CSWP_ASYNC_MESSAGE.");

    int rsp_len =  (rsp_type.size()           + 
                    1 /* size of errcode   */ +
                    1 /* num_regs          */ +
                    regID0.size()             +
                    regName0.size()           +
                    1 /* regsize           */ +
                    regName0.size()           + // display name
                    regDescription0.size()    +
                    regID1.size()             +
                    regName1.size()           +
                    1 /* regsize           */ +
                    regName1.size()           + // display name
                    regDescription1.size()    +
                    regID2.size()             +
                    regName2.size()           +
                    1 /* regsize           */ +
                    regName2.size()           + // display name
                    regDescription2.size()    +
                    regID3.size()             +
                    regName3.size()           +
                    1 /* regsize           */ +
                    regName3.size()           + // display name
                    regDescription3.size());
 
    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    rsp_bytes[offset] = num_regs;
    offset++;
    memcpy(rsp_bytes + offset, regID0.data(), regID0.size());
    offset += regID0.size();
    memcpy(rsp_bytes + offset, regName0.data(), regName0.size());
    offset += regName0.size();
    rsp_bytes[offset] = regSize;
    offset++;
    memcpy(rsp_bytes + offset, regName0.data(), regName0.size());
    offset += regName0.size();
    memcpy(rsp_bytes + offset, regDescription0.data(), regDescription0.size());
    offset += regDescription0.size();
    memcpy(rsp_bytes + offset, regID1.data(), regID1.size());
    offset += regID1.size();
    memcpy(rsp_bytes + offset, regName1.data(), regName1.size());
    offset += regName1.size();
    rsp_bytes[offset] = regSize;
    offset++;
    memcpy(rsp_bytes + offset, regName1.data(), regName1.size());
    offset += regName1.size();
    memcpy(rsp_bytes + offset, regDescription1.data(), regDescription1.size());
    offset += regDescription1.size();
    memcpy(rsp_bytes + offset, regID2.data(), regID2.size());
    offset += regID2.size();
    memcpy(rsp_bytes + offset, regName2.data(), regName2.size());
    offset += regName2.size();
    rsp_bytes[offset] = regSize;
    offset++;
    memcpy(rsp_bytes + offset, regName2.data(), regName2.size());
    offset += regName2.size();
    memcpy(rsp_bytes + offset, regDescription2.data(), regDescription2.size());
    offset += regDescription2.size();
    memcpy(rsp_bytes + offset, regID3.data(), regID3.size());
    offset += regID3.size();
    memcpy(rsp_bytes + offset, regName3.data(), regName3.size());
    offset += regName3.size();
    rsp_bytes[offset] = regSize;
    offset++;
    memcpy(rsp_bytes + offset, regName3.data(), regName3.size());
    offset += regName3.size();
    memcpy(rsp_bytes + offset, regDescription3.data(), regDescription3.size());
    offset += regDescription3.size();
 
    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_get_dev_reg
//----------------------------------------------
int
fudge_get_dev_reg(cmdinfo info) {
    uint64_t count;
    
    if (info.apnum != 0 and info.apnum != 2) {
        cout << "- ERROR: Invalid device_id " << info.apnum << ". Only device 0 and 2 are supported." << endl;
        return CSWP_REG_FAILED;
    }

    size_t inoffset = 0;
    inoffset += process_incoming_varint(info.data+inoffset, nullptr); // device_id
    inoffset += process_incoming_varint(info.data+inoffset, &count); // count

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;
    vector<uint8_t> rsp_count   = encode_varint(count);

    vector<uint32_t> rd_values = {};

    for(size_t i = 0; i < count; i++) {
        uint64_t register_id;
        inoffset += process_incoming_varint(info.data+inoffset, &register_id);
        if ((info.apnum == 0) && (register_id == 0xd00)) {
            rd_values.push_back(0);
        } else if ((info.apnum == 0) && (register_id == 0xdf8)) {
            rd_values.push_back(0);
        } else if ((info.apnum == 0) && (register_id == 0xdfc)) {
            rd_values.push_back(0x44770006);
        } else if ((info.apnum == 0) && (register_id == 0x4e560000)) {
            rd_values.push_back(0);
        } else if (register_id == 0xdfc) {
            rd_values.push_back(0);
        } else {
            rd_values.push_back(0x01020304);
        }
    }

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   rsp_count.size()              + 
                   rd_values.size()*sizeof(uint32_t));

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, rsp_count.data(), rsp_count.size());
    offset += rsp_count.size();
    memcpy(rsp_bytes + offset, rd_values.data(), rd_values.size()*sizeof(uint32_t));
    offset += rd_values.size()*sizeof(uint32_t);

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_set_dev_reg
//----------------------------------------------
int
fudge_set_dev_reg(cmdinfo info) {

    if (info.apnum != 0 and info.apnum != 2) {
        cout << "- ERROR: Invalid device_id " << info.apnum << ". Only device 0 and 2 are supported." << endl;
        return CSWP_REG_FAILED;
    }

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */);

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_get_dev_mem
//----------------------------------------------
int
fudge_get_dev_mem(cmdinfo info) {
    uint64_t address;
    uint64_t size;
    
    if (info.apnum != 0 and info.apnum != 1) {
        cout << "- ERROR: Invalid device_id " << info.apnum << ". Only device 0 and 1 are supported." << endl;
        return -1;
    }

    size_t inoffset = 0;
    inoffset += process_incoming_varint(info.data+inoffset, nullptr); // device_id
    inoffset += process_incoming_varint(info.data+inoffset, &address); // address
    inoffset += process_incoming_varint(info.data+inoffset, &size); // size

    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;
    vector<uint8_t> count       = encode_varint(size);

    vector<uint8_t> rd_values;

    for (size_t i=1; i <= size; i++) {
        rd_values.push_back(i);
    }

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */ +
                   count.size()              + 
                   rd_values.size());

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;
    offset++;
    memcpy(rsp_bytes + offset, count.data(), count.size());
    offset += count.size();
    memcpy(rsp_bytes + offset, rd_values.data(), rd_values.size());
    offset += rd_values.size();

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
    return 0;
}

//----------------------------------------------
// fudge_set_dev_mem
//----------------------------------------------
int
fudge_set_dev_mem(cmdinfo info) {

    if (info.apnum != 0 and info.apnum != 1) {
        cout << "- ERROR: Invalid device_id " << info.apnum << ". Only device 0 and 1 are supported." << endl;
        return -1;
    }
    vector<uint8_t> rsp_type    = encode_varint(info.cmd_enc);
    uint8_t         errcode     = 0x0;

    int rsp_len = (rsp_type.size()           + 
                   1 /* size of errcode   */);

    uint8_t* rsp_bytes = new uint8_t[rsp_len];
    int offset = 0;
    memcpy(rsp_bytes + offset, rsp_type.data(), rsp_type.size());
    offset += rsp_type.size();
    rsp_bytes[offset] = errcode;

    response_add(rsp_bytes, rsp_len);
    delete[] rsp_bytes;
   
    return 0;
}

// Unordered map of command encoding and function pointers
unordered_map <int, ccufun_ptr> funcMap = {
    {CSWP_INIT, fudge_init_cswp},
    {CSWP_TERM, fudge_term_cswp},
    {CSWP_GET_DEVICES, fudge_get_devices},
    {CSWP_GET_SYSTEM_DESCRIPTION, fudge_get_sys_info},
    {CSWP_DEVICE_OPEN, fudge_open_dev},
    {CSWP_DEVICE_CLOSE, fudge_close_dev},
    {CSWP_GET_DEVICE_CAPABILITIES, fudge_get_dev_cap},
    {CSWP_REG_LIST, fudge_get_reg_list},
    {CSWP_REG_READ, fudge_get_dev_reg},
    {CSWP_REG_WRITE, fudge_set_dev_reg},
    {CSWP_MEM_READ, fudge_get_dev_mem},
    {CSWP_MEM_WRITE, fudge_set_dev_mem}
};


//----------------------------------------------
// main
//----------------------------------------------
extern "C" int
main(int argc, char* argv[]) {

    int server_fd, client_socket;
    uint8_t buffer[4096] = {0};
    bool running = true;

    prog_name = argv[0];
    int status = start_server(&server_fd, &client_socket);
    if (status != 0) {
        return status;
    }

    // Process commands
    while (running) {
        // Clear buffer
        memset(buffer, 0, sizeof(buffer));

        // Read incoming message
        int bytes_read = ::read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            //print_msg("- Client disconnected");
            break;
        } else if (bytes_read <= 6) {
            // first byte of cmd_type (first few are msg_len, num_cmd, batch)
            print_msg("- ERROR: Received req data corrupted.");
            break;
        }
        //printdata(buffer, bytes_read);
            
        // Process received message
        cmdinfo *info_array;
        uint8_t error_mode;
        int ncmds = process_incoming_data(buffer, &info_array, &error_mode);
        if (ncmds < 0) { break; }

        bool cancel;
        int i;
        for(i = 0, cancel = false; i < ncmds; i ++) {
            int res = CSWP_UNSUPPORTED;
            cmdinfo info = info_array[i];
            if (cancel) {
                res = CSWP_CANCELLED;
            } else {
                auto it = funcMap.find(info.cmd_enc);
                if (it != funcMap.end())
                    res = it->second(info);
            }
            if (res != CSWP_SUCCESS) {
                response_err(info, res);
                if (error_mode)
                    cancel = true;
            }
            // Check for exit command
            if (info.cmd_enc == CSWP_TERM) {
                cout << "Exit command received. Server shutting down. Goodbye!";
                running = false;
                cancel = true;
            }
        }
        response_send(client_socket);
        free(info_array);
    }
    
    stop_server(server_fd, client_socket);

    return 0;
}
