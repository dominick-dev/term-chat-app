#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// message types
// need to set enum type under the hood
typedef enum
{
    C2S_NEW_CLIENT
} MessageType;

// message header
typedef struct
{
    MessageType message_type;
    uint16_t payload_length;
    uint16_t sequence_number;
    uint8_t version;
} MessageHeader;

//

#endif
