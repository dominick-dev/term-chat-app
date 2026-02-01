#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTOCOL_VERSION 1

// message types
// need to set enum type under the hood
typedef enum
{
    C2S_NEW_CLIENT
} MessageType;

// generic message header
typedef struct
{
    uint8_t message_type; // cast enum to uint8_t
    uint32_t payload_length;
    uint16_t sequence_number;
    uint8_t version;
} MessageHeader;

// different message type payloads
typedef struct
{
    char username[20];
} NewClientMsgPayload;

// union on each message payload
typedef union
{
    NewClientMsgPayload new_client;
} MessagePayload;

// generic message type
typedef struct
{
    MessageHeader header;
    MessagePayload payload;
} Message;

// serialize msgs
void serialize(Message* msg);
void serialize_header(MessageHeader* header);
void serialize_new_client_msg();

// deserialize msgs
void deserialize(uint8_t* msg_buffer, Message* msg);

// helpers
void print_message(Message* msg);

#endif
