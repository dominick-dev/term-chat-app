#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_VERSION 1

#define HEADER_SIZE 12
#define NEW_CLIENT_MSG_PAYLOAD_SIZE 21
#define ROOM_INFO_SIZE 23

#define MAX_CLIENTS 100 // does not include listening socket
#define MAX_ROOMS 100
#define MAX_CHAT_MSG_SIZE 256

typedef struct
{
    uint16_t server_id;
    uint8_t server_name[21];
} RoomInfo;

// receive state machine
typedef struct
{
    // header state
    uint8_t head_buff[HEADER_SIZE];
    size_t head_bytes_recv;

    // payload state
    uint8_t* pay_buff;
    size_t pay_expected_bytes;
    size_t pay_bytes_recv;

    // stages
    enum
    {
        EMPTY,           // on init
        WAITING,         // initialized, no current msg being processed
        READING_HEADER,  // header being read
        READING_PAYLOAD, // payload being read
    } ActiveState;
} RecvState;

// message types
// need to set enum type under the hood
typedef enum
{
    C2S_NEW_CLIENT,
    S2C_ROOM_LIST,
    C2S_CREATE_ROOM,
    C2S_JOIN_ROOM,
    S2C_JOIN_ROOM_RES,
    C2S_NEW_MSG,
    S2C_BROADCAST_MSG
} MessageType;

// generic message header
typedef struct
{
    uint8_t message_type; // cast enum to uint8_t
    uint32_t payload_length;
    uint16_t sequence_number;
    uint8_t version;
    uint32_t client_id;
} MessageHeader;

// different message type payloads
typedef struct
{
    uint8_t username[21];
} NewClientMsgPayload;

typedef struct
{
    uint8_t num_active_rooms;
    RoomInfo rooms[MAX_ROOMS];
} RoomListPayload;

typedef struct
{
    uint8_t server_name[21];
} CreateRoomPayload;

typedef struct
{
    RoomInfo room_to_join;
} JoinRoomPayload;

typedef struct
{
    uint8_t joined_result;
    RoomInfo joined_room;
} JoinRoomResPayload;

typedef struct
{
    RoomInfo target_room;
    uint8_t username[21];
    char msg[MAX_CHAT_MSG_SIZE];
} NewMsgPayload;

// union on each message payload
typedef union
{
    NewClientMsgPayload new_client;
    RoomListPayload room_list;
    CreateRoomPayload create_room;
    JoinRoomPayload join_room;
    JoinRoomResPayload join_room_res;
    NewMsgPayload new_msg;
} MessagePayload;

// generic message type
typedef struct
{
    MessageHeader header;
    MessagePayload payload;
} Message;

// serialize msgs
void serialize(Message* msg, uint8_t* buff);
void serialize_header(MessageHeader* header);
void serialize_new_client_msg();

// deserialize msgs
void deserialize_header(uint8_t* headr_buffer, Message* msg);
int deserialize_payload(uint8_t* payload_buffer, Message* msg);

// helpers
int recv_message(int socket_fd, RecvState* state, Message* msg);
void print_header(Message* msg);
void show_buff_hex(uint8_t* buff, size_t actual_size);

#endif
