#ifndef SERVER_H
#define SERVER_H

#include "../../include/protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PORT 8080
#define MESSAGE_SIZE 255

typedef struct
{
    bool isActive;
    uint16_t server_id;
    uint8_t server_name[21];
    uint32_t joined_client_ids[MAX_CLIENTS];
    uint8_t num_clients_in_room;
} ServerRoom;

typedef struct
{
    RecvState recv_state;
    SOCKET_T socketfd;
    char username[21];
    uint32_t clientId;
    uint16_t joined_server_id;
} ClientState;

#endif
