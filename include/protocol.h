#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

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
    uint16_t payload_length;
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
void serialize_header(MessageHeader* header);
void serialize_new_client_msg(Message* msg);

// deserialize msgs
void deserialize_header(MessageHeader* header);
void deserialize_new_client_msg();

#endif
