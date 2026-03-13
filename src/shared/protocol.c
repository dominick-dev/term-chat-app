#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../include/protocol.h"

#define PAYLOAD_LENGTH_POSITION 1
#define BUFFER_SIZE 1024 * 64 // 64KB

/*
 * Receives full message from socket_fd, updates internal state, deserializes and creates msg
 * Returns 1 if complete message, 0 if partial, -1 if error/disconnect
 */
int recv_message(int socket_fd, RecvState* state, Message* msg)
{
    uint8_t* header_buff = NULL;
    ssize_t bytes = 0;

    // switch on possible state
    switch (state->ActiveState)
    {
    case (EMPTY):
        perror("Recv_message with empty state, something is wrong");
        return -1;
    case (WAITING):
        // read header
        header_buff = calloc(HEADER_SIZE, sizeof(uint8_t));
        bytes = recv(socket_fd, header_buff, HEADER_SIZE, 0);

        // leave, no msg to parse
        if (bytes == 0)
        {
            printf("Recv 0 bytes from (%i), orderly shutdown\n", socket_fd);

            // clean up
            state->ActiveState = EMPTY; // is this necessary?
            free(header_buff);
            return -1;
        }

        // set active state
        if (bytes == HEADER_SIZE)
        {
            state->ActiveState = READING_PAYLOAD;

            // check if payload is empty
            uint32_t net_len;
            memcpy(&net_len, header_buff + PAYLOAD_LENGTH_POSITION, sizeof(uint32_t));
            net_len = ntohl(net_len);

            // no payload to be read
            if (net_len == 0)
            {
                state->ActiveState = WAITING;

                memcpy(state->head_buff, header_buff, HEADER_SIZE);
                deserialize_header(state->head_buff, msg);

                // reset state for next message
                state->pay_bytes_recv = 0;
                state->pay_expected_bytes = 0;
                state->head_bytes_recv = 0;
                state->ActiveState = WAITING;

                free(header_buff);

                return 1;
            }
        }
        else
        {
            state->ActiveState = READING_HEADER;
        }

        // copy buff over to state & free temp buff
        memcpy(state->head_buff, header_buff, HEADER_SIZE);
        state->head_bytes_recv += bytes;
        free(header_buff);

        return 0;
    case (READING_HEADER):
        // continue reading header, change active state if needed
        header_buff = calloc(HEADER_SIZE, sizeof(uint8_t));
        bytes = recv(socket_fd, header_buff, HEADER_SIZE - state->head_bytes_recv, 0);

        // leave, no msg to parse
        if (bytes == 0)
        {
            printf("Recv 0 bytes from (%i), orderly shutdown\n", socket_fd);

            // clean up
            state->ActiveState = EMPTY; // is this necessary?
            free(header_buff);
            return -1;
        }

        // set active state
        if (bytes + state->head_bytes_recv == HEADER_SIZE)
        {
            state->ActiveState = READING_PAYLOAD;
        }
        else
        {
            // still reading header, just need logging otherwise can remove this case
        }

        memcpy(state->head_buff + state->head_bytes_recv, header_buff, bytes);
        state->head_bytes_recv += bytes;
        free(header_buff);

        return 0;
    case (READING_PAYLOAD):
        // first time reading payload
        if (state->pay_expected_bytes == 0)
        {
            uint32_t net_let;
            memcpy(&net_let, state->head_buff + sizeof(uint8_t), sizeof(uint32_t));
            state->pay_expected_bytes = ntohl(net_let);

            if (state->pay_expected_bytes == 0)
            {
                deserialize_header(state->head_buff, msg);

                // reset state for next message
                state->pay_bytes_recv = 0;
                state->pay_expected_bytes = 0;
                state->head_bytes_recv = 0;
                state->ActiveState = WAITING;

                return 1;
            }

            state->pay_buff = calloc(state->pay_expected_bytes, 1);
        }

        uint32_t payload_len = state->pay_expected_bytes;

        bytes = recv(socket_fd, state->pay_buff + state->pay_bytes_recv, payload_len - state->pay_bytes_recv, 0);

        // leave, no msg to parse
        if (bytes == 0)
        {
            printf("Recv 0 bytes from (%i), orderly shutdown\n", socket_fd);

            // clean up
            state->ActiveState = EMPTY; // is this necessary?
            free(header_buff);
            free(state->pay_buff);
            return -1;
        }

        if (bytes + state->pay_bytes_recv == payload_len)
        {
            state->ActiveState = WAITING;

            // deserialize message
            deserialize_header(state->head_buff, msg);
            deserialize_payload(state->pay_buff, msg);

            // reset state for next message
            free(state->pay_buff);
            state->pay_buff = NULL;
            state->pay_bytes_recv = 0;
            state->pay_expected_bytes = 0;
            state->head_bytes_recv = 0;
            state->ActiveState = WAITING;

            return 1;
        }
        else
        {
            // just need logging here for when there are more pay bytes to read?
        }

        state->pay_bytes_recv += bytes;

        return 0;
    default:
        printf("Invalid messge type\n");
        return -1;
    }
}

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

    uint32_t client_id = htonl(header.client_id);
    memcpy(p, &client_id, sizeof(uint32_t));
    p += sizeof(uint32_t);

    uint32_t len = 0;
    // switch on msg type, call serialize payload for that msg type (should return length)
    switch (header.message_type)
    {
    case C2S_NEW_CLIENT:
    {
        // serialize payload
        uint32_t length = strlen((const char*)payload.new_client.username) + 1;
        memcpy(p, payload.new_client.username, length);

        // fill in length in header
        len = htonl(length);
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &len, sizeof(uint32_t));
        break;
    }
    case S2C_ROOM_LIST:
        // no active rooms
        if (payload.room_list.num_active_rooms == 0)
        {
            // serialize len in header
            uint32_t zero_len = 0;
            memcpy(buff + PAYLOAD_LENGTH_POSITION, &zero_len, sizeof(uint32_t));
            break;
        }

        // some active rooms
        len = htonl(sizeof(uint8_t) + payload.room_list.num_active_rooms * (sizeof(uint16_t) + 21));
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &len, sizeof(uint32_t));

        memcpy(p, &payload.room_list.num_active_rooms, sizeof(uint8_t));
        p += sizeof(uint8_t);

        for (int i = 0; i < payload.room_list.num_active_rooms; i++)
        {
            // each room has server id (16) and server_name (8)
            RoomInfo curr_room = payload.room_list.rooms[i];

            // have to convert to network order
            uint16_t n_server_id = htons(curr_room.server_id);
            memcpy(p, &n_server_id, sizeof(uint16_t));
            p += sizeof(uint16_t);

            memcpy(p, curr_room.server_name, sizeof(uint8_t) * 21);
            p += (sizeof(uint8_t) * 21);
        }

        break;
    case C2S_CREATE_ROOM:;
        uint32_t net_len = htonl(header.payload_length);
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &net_len, sizeof(uint32_t));

        memcpy(p, payload.create_room.server_name, header.payload_length);

        break;
    case C2S_JOIN_ROOM:;
        uint32_t join_len = htonl(ROOM_INFO_SIZE);
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &join_len, sizeof(uint32_t));

        // payload is single RoomInfo
        uint16_t n_server_id = htons(payload.join_room.room_to_join.server_id);
        memcpy(p, &n_server_id, sizeof(uint16_t));
        p += sizeof(uint16_t);

        memcpy(p, payload.join_room.room_to_join.server_name, sizeof(uint8_t) * 21);
        p += (sizeof(uint8_t) * 21);

        break;
    case S2C_JOIN_ROOM_RES:;
        len = htonl(header.payload_length);
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &len, sizeof(uint32_t));

        uint8_t bool_val = payload.join_room_res.joined_result ? 1 : 0;
        memcpy(p, &bool_val, sizeof(uint8_t));
        p += sizeof(uint8_t);

        n_server_id = htons(payload.join_room_res.joined_room.server_id);
        memcpy(p, &n_server_id, sizeof(uint16_t));
        p += sizeof(uint16_t);

        memcpy(p, payload.join_room_res.joined_room.server_name, sizeof(uint8_t) * 21);
        p += (sizeof(uint8_t) * 21);

        break;
    case C2S_NEW_MSG:
    case S2C_BROADCAST_MSG:
        len = htonl(header.payload_length);
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &len, sizeof(uint32_t));

        n_server_id = htons(payload.new_msg.target_room.server_id);
        memcpy(p, &n_server_id, sizeof(uint16_t));
        p += sizeof(uint16_t);

        memcpy(p, payload.new_msg.target_room.server_name, sizeof(uint8_t) * 21);
        p += (sizeof(uint8_t) * 21);

        memcpy(p, payload.new_msg.username, 21);
        p += (sizeof(uint8_t) * 21);

        // TODO: hardcode struct serial sizes that are sent over wire and use those to fix magic numbers like here
        memcpy(p, payload.new_msg.msg, header.payload_length - ROOM_INFO_SIZE - 21);
        p += (sizeof(uint8_t) * header.payload_length - ROOM_INFO_SIZE - 21);

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
    p += sizeof(uint8_t);

    uint32_t client_id;
    memcpy(&client_id, headr_buffer + p, sizeof(uint32_t));
    msg->header.client_id = ntohl(client_id);
    p += sizeof(uint32_t);
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
    case S2C_ROOM_LIST:
    {
        uint8_t num_active_rooms;
        memcpy(&num_active_rooms, payload_buffer + p, sizeof(uint8_t));
        msg->payload.room_list.num_active_rooms = num_active_rooms;
        p += sizeof(uint8_t);

        for (int i = 0; i < num_active_rooms; i++)
        {
            uint16_t server_id;
            memcpy(&server_id, payload_buffer + p, sizeof(uint16_t));
            msg->payload.room_list.rooms[i].server_id = ntohs(server_id);
            p += sizeof(uint16_t);

            memcpy(msg->payload.room_list.rooms[i].server_name, payload_buffer + p, sizeof(uint8_t) * 21);
            p += (sizeof(uint8_t) * 21);
        }

        break;
    }
    case C2S_CREATE_ROOM:
        memcpy(msg->payload.create_room.server_name, payload_buffer + p, msg->header.payload_length);

        break;
    case C2S_JOIN_ROOM:;
        uint16_t server_id;
        memcpy(&server_id, payload_buffer + p, sizeof(uint16_t));
        msg->payload.join_room.room_to_join.server_id = ntohs(server_id);
        p += sizeof(uint16_t);

        memcpy(msg->payload.join_room.room_to_join.server_name, payload_buffer + p, sizeof(uint8_t) * 21);
        p += (sizeof(uint8_t) * 21);

        break;
    case S2C_JOIN_ROOM_RES:;
        uint8_t bool_val;
        memcpy(&bool_val, payload_buffer + p, sizeof(uint8_t));
        msg->payload.join_room_res.joined_result = (bool_val != 0);
        p += sizeof(uint8_t);

        uint16_t s_id;
        memcpy(&s_id, payload_buffer + p, sizeof(uint16_t));
        msg->payload.join_room_res.joined_room.server_id = ntohs(s_id);
        p += sizeof(uint16_t);

        memcpy(msg->payload.join_room_res.joined_room.server_name, payload_buffer + p, sizeof(uint8_t) * 21);
        p += (sizeof(uint8_t) * 21);

        break;
    case C2S_NEW_MSG:
    case S2C_BROADCAST_MSG:;
        // TODO: fix case where same variable is initialized in multiple cases, can't all have same name
        uint16_t serv_id;
        memcpy(&serv_id, payload_buffer + p, sizeof(uint16_t));
        msg->payload.new_msg.target_room.server_id = ntohs(serv_id);
        p += sizeof(uint16_t);

        memcpy(msg->payload.new_msg.target_room.server_name, payload_buffer + p, sizeof(uint8_t) * 21);
        p += (sizeof(uint8_t) * 21);

        memcpy(msg->payload.new_msg.username, payload_buffer + p, 21);
        p += (sizeof(uint8_t) * 21);

        // 21 accounts for username
        memcpy(msg->payload.new_msg.msg, payload_buffer + p, msg->header.payload_length - ROOM_INFO_SIZE - 21);
        p += (sizeof(uint8_t) * msg->header.payload_length - ROOM_INFO_SIZE - 21);

        break;
    default:
        printf("Unknown message type, cannot, deserialize the payload!\n");
    }
}
