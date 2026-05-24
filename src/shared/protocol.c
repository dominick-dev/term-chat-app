#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/protocol.h"

#define PAYLOAD_LENGTH_POSITION 1
#define BUFFER_SIZE 1024 * 64 // 64KB

/*
 * Receives full message from socket_fd, updates internal state, deserializes and creates msg
 * Returns 1 if complete message, 0 if partial, -1 if error/disconnect
 */
int recv_message(SOCKET_T socket_fd, RecvState* state, Message* msg)
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
        if (bytes <= 0)
        {
            if (bytes == 0)
            {
                printf("Recv 0 bytes from (%i), orderly shutdown\n", socket_fd);
            }
            else
            {
                printf("recv error\n");
            }

            // clean up
            state->ActiveState = EMPTY;
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
        if (bytes <= 0)
        {
            if (bytes == 0)
            {
                printf("Recv 0 bytes from (%i), orderly shutdown\n", socket_fd);
            }
            else
            {
                printf("recv error\n");
            }

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

            // enforce strict upper bound on payload size, 64KB
            if (state->pay_expected_bytes > 65536)
            {
                return -1;
            }

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
        if (bytes <= 0)
        {
            if (bytes == 0)
            {
                printf("Recv 0 bytes from (%i), orderly shutdown\n", socket_fd);
            }
            else
            {
                printf("recv error\n");
            }

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
            if (deserialize_payload(state->pay_buff, msg) != 0)
            {
                free(state->pay_buff);
                state->pay_buff = NULL;
                return -1;
            }

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
        memcpy(p, payload.new_client.username, NEW_CLIENT_MSG_PAYLOAD_SIZE);

        // fill in length in header
        len = htonl(NEW_CLIENT_MSG_PAYLOAD_SIZE);
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
    case C2S_LEAVE:;
        uint32_t zero_len = 0;
        memcpy(buff + PAYLOAD_LENGTH_POSITION, &zero_len, sizeof(uint32_t));
        p += sizeof(uint32_t);

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

int deserialize_payload(uint8_t* payload_buffer, Message* msg)
{
    size_t p = 0;
    uint16_t server_id;
    uint32_t pay_len = msg->header.payload_length;

    switch (msg->header.message_type)
    {
    case C2S_NEW_CLIENT:
        if (pay_len != NEW_CLIENT_MSG_PAYLOAD_SIZE)
        {
            return -1;
        }

        memcpy(msg->payload.new_client.username, payload_buffer + p, NEW_CLIENT_MSG_PAYLOAD_SIZE);
        msg->payload.new_client.username[20] = '\0';

        break;
    case S2C_ROOM_LIST:
    {
        if (pay_len < sizeof(uint8_t))
        {
            return -1;
        }
        uint8_t num_active_rooms = payload_buffer[0];
        if (pay_len != sizeof(uint8_t) + (num_active_rooms * 23))
        {
            return -1; // mismatched or truncated payload size
        }

        memcpy(&num_active_rooms, payload_buffer + p, sizeof(uint8_t));

        if (num_active_rooms > MAX_ROOMS)
        {
            return -1;
        }

        msg->payload.room_list.num_active_rooms = num_active_rooms;
        p += sizeof(uint8_t);

        for (int i = 0; i < num_active_rooms; i++)
        {
            memcpy(&server_id, payload_buffer + p, sizeof(uint16_t));
            msg->payload.room_list.rooms[i].server_id = ntohs(server_id);
            p += sizeof(uint16_t);

            memcpy(msg->payload.room_list.rooms[i].server_name, payload_buffer + p, sizeof(uint8_t) * 21);
            msg->payload.room_list.rooms[i].server_name[20] = '\0';
            p += (sizeof(uint8_t) * 21);
        }

        break;
    }
    case C2S_CREATE_ROOM:
        if (pay_len != 21)
        {
            return -1;
        }

        memcpy(msg->payload.create_room.server_name, payload_buffer + p, 21);
        msg->payload.create_room.server_name[20] = '\0';

        break;
    case C2S_JOIN_ROOM:
        if (pay_len != ROOM_INFO_SIZE)
        {
            return -1;
        }

        memcpy(&server_id, payload_buffer + p, sizeof(uint16_t));
        msg->payload.join_room.room_to_join.server_id = ntohs(server_id);
        p += sizeof(uint16_t);

        memcpy(msg->payload.join_room.room_to_join.server_name, payload_buffer + p, sizeof(uint8_t) * 21);
        msg->payload.join_room.room_to_join.server_name[20] = '\0';
        p += (sizeof(uint8_t) * 21);

        break;
    case S2C_JOIN_ROOM_RES:;
        // 1 pay bol + 2 byte id + 21 byte name
        if (pay_len != 24)
        {
            return -1;
        }

        uint8_t bool_val;
        memcpy(&bool_val, payload_buffer + p, sizeof(uint8_t));
        msg->payload.join_room_res.joined_result = (bool_val != 0);
        p += sizeof(uint8_t);

        memcpy(&server_id, payload_buffer + p, sizeof(uint16_t));
        msg->payload.join_room_res.joined_room.server_id = ntohs(server_id);
        p += sizeof(uint16_t);

        memcpy(msg->payload.join_room_res.joined_room.server_name, payload_buffer + p, sizeof(uint8_t) * 21);
        msg->payload.join_room_res.joined_room.server_name[20] = '\0';
        p += (sizeof(uint8_t) * 21);

        break;
    case C2S_NEW_MSG:
    case S2C_BROADCAST_MSG:;
        if (pay_len < ROOM_INFO_SIZE + 21)
        {
            return -1;
        }

        size_t msg_text_len = pay_len - ROOM_INFO_SIZE - 21;

        if (msg_text_len > MAX_CHAT_MSG_SIZE - 1)
        {
            return -1;
        }

        memcpy(&server_id, payload_buffer + p, sizeof(uint16_t));
        msg->payload.new_msg.target_room.server_id = ntohs(server_id);
        p += sizeof(uint16_t);

        memcpy(msg->payload.new_msg.target_room.server_name, payload_buffer + p, sizeof(uint8_t) * 21);
        msg->payload.new_msg.target_room.server_name[20] = '\0';
        p += (sizeof(uint8_t) * 21);

        memcpy(msg->payload.new_msg.username, payload_buffer + p, 21);
        msg->payload.new_msg.username[20] = '\0';
        p += (sizeof(uint8_t) * 21);

        // 21 accounts for username
        memcpy(msg->payload.new_msg.msg, payload_buffer + p, sizeof(uint8_t) * msg_text_len);
        msg->payload.new_msg.msg[msg_text_len] = '\0';
        p += (sizeof(uint8_t) * msg_text_len);

        break;
    case C2S_LEAVE:
        if (pay_len != 0)
        {
            return -1;
        }

        break;
    default:
        printf("Unknown message type, cannot, deserialize the payload!\n");
        return -1;
    }

    return 0;
}
