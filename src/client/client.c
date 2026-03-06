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
    IN_ROOM
} ChatState;

typedef struct
{
    RecvState state;
    char username[21];
    uint32_t id;
    uint16_t sequence_num;
    ChatState chat_state;
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

    printf("Welcome: %s - lets get you chatting\n\n", profile.username);
}

static void send_new_join(int* socketfd)
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
    send_res = send(*socketfd, buff, HEADER_SIZE + strlen(profile.username) + 1, 0);
    if (send_res < 0)
    {
        perror("Error sending msg to server");
    }

    printf("Msg sent: %i bytes sent\n", send_res);
    free(buff);
}

void handle_create_room(Message* msg)
{
    // need new message type
    // payload of room name (can only be 20 chars + 1 null term)
    printf("handle_create_room called\n");
}

void route_server_message(Message* msg)
{
    switch (msg->header.message_type)
    {
    case S2C_ROOM_LIST:
        printf("Routing create room message from server\n");
        handle_create_room(msg);
        break;
    default:
        printf("Unrecognized message type: %d\n", msg->header.message_type);
    }
}

int main()
{
    int socketfd = -1;
    FILE* sin = stdin;

    setup_user(sin);
    client_init(&socketfd);
    send_new_join(&socketfd);

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
                // next step would be to send to server
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
