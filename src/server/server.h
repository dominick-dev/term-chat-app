#ifndef SERVER_H
#define SERVER_H

#include "../../include/protocol.h"
#include <stddef.h>
#include <stdint.h>

#define PORT 8080
#define MAX_CLIENTS 100 // does not include listening socket
#define MAX_ROOMS 100
#define MESSAGE_SIZE 255

typedef struct
{
    // header state
    uint8_t head_buff[HEADER_SIZE];
    size_t head_bytes_recv;

    // payload state
    uint8_t* pay_buff;
    size_t pay_expected_bytes;
    size_t pay_bytes_recv;

    // active client state
    enum
    {
        EMPTY,          // on init
        WAITING,        // initialized, no current msg being processed
        READING_HEADER, // header being read
        READING_PAYLOAD // payload being read
    } ActiveState;

    // server related state
    uint8_t username[21];
    uint32_t clientId;
    uint16_t joined_server_id;
} ClientState;

typedef struct
{
    uint16_t server_id;
    uint32_t joined_client_ids[MAX_CLIENTS];
} ServerRoom;

#endif
