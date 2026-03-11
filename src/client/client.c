#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../include/logger.h"
#include "../../include/protocol.h"

#define PORT 8080
#define MESSAGE_SIZE 256
#define MAX_USERNAME_LEN 21   // includes \0
#define MAX_SERVERNAME_LEN 21 // includes \0

typedef enum
{
    AWAITING_ROOM_LIST,
    IN_ROOM_MENU,
    CREATING_ROOM,
    IN_ROOM
} ChatState;

typedef struct
{
    RecvState state;
    char username[21];
    uint32_t id;
    uint16_t sequence_num;
    ChatState chat_state;
    uint8_t num_active_rooms;
    RoomInfo rooms[MAX_ROOMS];
} ClientState;

static ClientState profile = {0};

/*
 * Initializes the client socket
 */
static void client_init(int* socketfd)
{
    LOG_INFO(__FUNCTION__, "initializing the client...\n");

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // create client socket
    *socketfd = socket(PF_INET, SOCK_STREAM, 0);
    if (*socketfd == -1)
    {
        perror("Error creating client socket");
        LOG_ERROR(__FUNCTION__, "Error creating client socket");
        exit(EXIT_FAILURE);
    }

    // attempt to connect to server socket
    int conn_res = connect(*socketfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (conn_res != 0)
    {
        perror("Error connecting to server socket");
        LOG_ERROR(__FUNCTION__, "Error connecting to server socket");
        exit(EXIT_FAILURE);
    }

    // made it here, connected to server
    LOG_INFO(__FUNCTION__, "Connected to server");
    printf("Connected to server!\n");
}

/*
 * Intro client to program, initialize client profile
 */
static void setup_user(FILE* sin)
{
    printf("Welcome to the chat app!!\n");
    printf("First, please input your username\n");

    fgets(profile.username, MAX_USERNAME_LEN, sin);
    profile.username[strcspn(profile.username, "\n")] = 0;
    profile.sequence_num = 0;
    profile.chat_state = AWAITING_ROOM_LIST;

    printf("Welcome: %s - lets get you chatting\n\n", profile.username);
}

static void send_new_join(int socketfd)
{
    int send_res = -1;

    // generate message header struct
    MessageHeader header = {0};
    header.message_type = C2S_NEW_CLIENT;
    header.payload_length = strlen(profile.username) + 1;
    header.sequence_number = ++profile.sequence_num;
    header.version = PROTOCOL_VERSION;
    header.client_id = 0; // will be assigned by server

    // generate message payload struct
    NewClientMsgPayload payload = {0};
    strcpy(payload.username, profile.username);

    // pack in generic message
    Message msg = {0};
    msg.header = header;
    msg.payload.new_client = payload;

    // serialize
    uint8_t* buff = calloc(1, sizeof(Message)); // larger than needed but that's okay?
    serialize(&msg, buff);

    // send new client msg to server
    printf("Sending new client message to server\n");
    send_res = send(socketfd, buff, HEADER_SIZE + strlen(profile.username) + 1, 0);
    if (send_res < 0)
    {
        perror("Error sending msg to server");
    }

    printf("Msg sent: %i bytes sent\n", send_res);
    free(buff);
}

void handle_room_list(Message* msg)
{
    printf("handle_create_room called\n");

    profile.chat_state = IN_ROOM_MENU;

    // 0 set to create new room
    printf("0 -> Create new room\n");

    // have active rooms to send
    int count = 1;
    if (msg->header.payload_length > 0)
    {
        profile.num_active_rooms = msg->payload.room_list.num_active_rooms;
        memcpy(profile.rooms, msg->payload.room_list.rooms, msg->payload.room_list.num_active_rooms * sizeof(RoomInfo));

        printf("Currently active rooms...\n");
        for (int i = 0; i < msg->payload.room_list.num_active_rooms; i++)
        {
            RoomInfo curr_room = msg->payload.room_list.rooms[i];
            printf("%i -> Room: \"%s\" (id: %i)\n", count, curr_room.server_name, curr_room.server_id);
            count++;
        }
    }
}

void route_server_message(Message* msg)
{
    switch (msg->header.message_type)
    {
    case S2C_ROOM_LIST:
        printf("Routing room list message from server\n");
        handle_room_list(msg);
        break;
    default:
        printf("Unrecognized message type: %d\n", msg->header.message_type);
    }
}

void send_join_room(int socketfd, int input)
{
    // create C2S_JOIN_ROOM message
    Message msg = {0};
    msg.header.message_type = C2S_JOIN_ROOM;
    msg.header.client_id = profile.id;
    msg.header.version = PROTOCOL_VERSION;
    msg.header.sequence_number = ++profile.sequence_num;
    msg.header.payload_length = ROOM_INFO_SIZE;

    RoomInfo room_to_join = profile.rooms[input - 1];
    msg.payload.join_room.room_to_join = room_to_join;

    // TODO: check send res
    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);
    send(socketfd, buff, HEADER_SIZE + ROOM_INFO_SIZE, 0);
    free(buff);
}

void send_create_room(int socketfd, char* input)
{
    Message msg = {0};
    msg.header.message_type = C2S_CREATE_ROOM;
    msg.header.client_id = profile.id;
    msg.header.version = PROTOCOL_VERSION;
    msg.header.sequence_number = ++profile.sequence_num;
    msg.header.payload_length = strlen(input);

    memcpy(msg.payload.create_room.server_name, input, strlen(input));
    msg.payload.create_room.server_name[20] = '\0';

    // TODO: check send res
    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);
    send(socketfd, buff, HEADER_SIZE + msg.header.payload_length, 0);
    free(buff);
}

int main()
{
    int socketfd = -1;
    FILE* sin = stdin;

    setup_user(sin);
    client_init(&socketfd);
    send_new_join(socketfd);

    struct pollfd pfds[2];

    profile.state.ActiveState = WAITING;

    // add server to pfds
    pfds[0].fd = socketfd;
    pfds[0].events = POLLIN;

    // add stdin to pfds
    pfds[1].fd = STDIN_FILENO;
    pfds[1].events = POLLIN;

    int num_polled = 0;

    while (1)
    {
        num_polled = poll(pfds, 2, -1);

        if (num_polled < 0)
        {
            perror("Error polling for new events");
        }
        else if (num_polled == 0) // shouldn't ever enter
        {
            printf("Nothing polled\n");
            continue;
        }

        for (int i = 0; i < 2; i++)
        {
            const struct pollfd curr_pfd = pfds[i];

            if (curr_pfd.revents == 0)
            {
                continue;
            }

            // user inputted new msg
            if ((curr_pfd.fd == STDIN_FILENO) && (curr_pfd.revents & POLLIN))
            {
                printf("Client inputted new msg:\n");

                // get input and remove newline
                char input[MESSAGE_SIZE];
                fgets(input, MESSAGE_SIZE, stdin);
                input[strcspn(input, "\n")] = 0;

                switch (profile.chat_state)
                {
                case AWAITING_ROOM_LIST:
                    // shouldn't be possible
                    break;
                case IN_ROOM_MENU:;
                    char* end;
                    long input_num = strtol(input, &end, 10);

                    // validate input
                    if (end != input && *end == '\0' &&
                        input_num >= 0 && input_num <= profile.num_active_rooms)
                    {
                        // making a new room
                        if (input_num == 0)
                        {
                            profile.chat_state = CREATING_ROOM;
                            printf("Enter a room name: \n");
                        }
                        // joining existing room
                        else
                        {
                            // send join request for selected room
                            send_join_room(socketfd, input_num);
                        }
                    }
                    else
                    {
                        printf("Selection must be >= 0 and <= %i, try again!\n", profile.num_active_rooms);
                    }
                    break;
                case CREATING_ROOM:
                    // chop input at 20 characters
                    if (strlen(input) >= MAX_SERVERNAME_LEN)
                    {
                        input[MAX_SERVERNAME_LEN - 1] = '\0';
                    }

                    send_create_room(socketfd, input);

                    break;
                case IN_ROOM:
                    break;
                default:
                    printf("Unrecognized chat state, resetting client...\n");
                    // TODO: add logic to reset client
                    break;
                }

                continue;
            }

            // new msg from server
            if ((curr_pfd.fd == socketfd) && (curr_pfd.revents & POLLIN))
            {
                Message msg = {0};
                int result = recv_message(curr_pfd.fd, &profile.state, &msg);
                printf("result: %i\n", result);
                if (result == -1)
                {
                    perror("Server disconnected\n");
                    exit(EXIT_FAILURE);
                }
                else if (result == 1)
                {
                    print_header(&msg);
                    printf("Full message received from server!\n");
                    // set client_id if not done yet
                    if (profile.id == 0)
                    {
                        profile.id = msg.header.client_id;
                        printf("My id is: %i\n", profile.id);
                    }

                    route_server_message(&msg);
                }

                continue;
            }

            // server hang up or error
            if ((curr_pfd.fd == socketfd) && (curr_pfd.revents & POLLHUP || curr_pfd.revents & POLLERR))
            {
                printf("Server closed due to error\n");
                continue;
            }
        }
    }

    // close client socket when done
    close(socketfd);
    printf("Socket closed\n");

    return 0;
}
