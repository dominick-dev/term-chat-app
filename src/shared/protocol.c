#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "../../include/protocol.h"

#define PAYLOAD_LENGTH_POSITION 1
#define BUFFER_SIZE 1024 * 64 // 64KB

void print_header(Message* msg)
{
    printf("Message type: %i\n", msg->header.message_type);
    printf("Sequence number: %i\n", msg->header.sequence_number);
    printf("Payload length: %i\n", msg->header.payload_length);
    printf("Version: %i\n", msg->header.version);
}

void show_buff_hex(uint8_t* buff, size_t actual_size)
{
    printf("Buffer contents:\n");
    for (size_t i = 0; i < actual_size; i++)
    {
        printf("%02x", buff[i]);
    }
    printf("\n");
}

/*
 * Serialize msg
 */
void serialize(Message* msg, uint8_t* buff)
{
    // init pointer for serialize steps
    uint8_t* p = buff;

    // get message header, serialize all but pay length
    MessageHeader header = msg->header;
    MessagePayload payload = msg->payload;

    memcpy(p, &header.message_type, sizeof(uint8_t));
    p += sizeof(uint8_t);

    // advance to account for payload length
    p += sizeof(uint32_t);

    uint16_t seq_num = htons(header.sequence_number);
    memcpy(p, &seq_num, sizeof(uint16_t));
    p += sizeof(uint16_t);

    memcpy(p, &header.version, sizeof(uint8_t));
    p += sizeof(uint8_t);

    // switch on msg type, call serialize payload for that msg type (should return length)
    switch (header.message_type)
    {
        // TODO: remove ; in line below, can't have declaration after :
    case C2S_NEW_CLIENT:;
        // serialize payload
        uint32_t length = strlen(payload.new_client.username) + 1;
        memcpy(p, payload.new_client.username, length);

        // fill in length
        uint32_t len = htonl(length);
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &len, sizeof(uint32_t));
        break;
    default:
        printf("Unknown message type, cannot serialize!\n");
    }
}

void deserialize_header(uint8_t* headr_buffer, Message* msg)
{
    size_t p = 0;

    memcpy(&msg->header.message_type, headr_buffer + p, sizeof(uint8_t));
    p += sizeof(uint8_t);

    uint32_t pay_len;
    memcpy(&pay_len, headr_buffer + p, sizeof(uint32_t));
    msg->header.payload_length = ntohl(pay_len);
    p += sizeof(uint32_t);

    uint16_t seq_num;
    memcpy(&seq_num, headr_buffer + p, sizeof(uint16_t));
    msg->header.sequence_number = ntohs(seq_num);
    p += sizeof(uint16_t);

    memcpy(&msg->header.version, headr_buffer + p, sizeof(uint8_t));
}

void deserialize_payload(uint8_t* payload_buffer, Message* msg)
{
    size_t p = 0;

    switch (msg->header.message_type)
    {
    case C2S_NEW_CLIENT:
        memcpy(msg->payload.new_client.username, payload_buffer + p, msg->header.payload_length);
        printf("\n");

        break;
    default:
        printf("Unknown message type, cannot, deserialize the payload!\n");
    }
}
