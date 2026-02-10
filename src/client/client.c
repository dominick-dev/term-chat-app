#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../include/logger.h"
#include "../../include/protocol.h"

#define PORT 8080
#define MESSAGE_SIZE 255

typedef struct
{
    char username[20];
    uint16_t sequence_num;
} ClientProfile;

static ClientProfile profile = {0};

/*
 * Initializes the client socket
 */
int client_init(int socketfd)
{
    LOG_INFO(__FUNCTION__, "initializing the client...\n");

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // create client socket
    socketfd = socket(PF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("Error creating client socket");
        LOG_ERROR(__FUNCTION__, "Error creating client socket");
        exit(EXIT_FAILURE);
    }

    // attempt to connect to server socket
    int conn_res = connect(socketfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (conn_res != 0)
    {
        perror("Error connecting to server socket");
        LOG_ERROR(__FUNCTION__, "Error connecting to server socket");
        exit(EXIT_FAILURE);
    }

    // made it here, connected to server
    LOG_INFO(__FUNCTION__, "Connected to server");
    printf("Connected to server!\n");

    return socketfd;
}

/*
 * Intro client to program, initialize client profile
 */
void setup_user(FILE* sin)
{
    printf("Welcome to the chat app!!\n");
    printf("First, please input your username\n");

    // TODO:make sure username length is checked
    // may be buggy need to make sure user inputted name is not too large
    // where is null terminator counted? 20 char + \0 is too large! same with \n
    fgets(profile.username, 20, sin);
    profile.username[strcspn(profile.username, "\n")] = 0;
    profile.sequence_num = 0;

    printf("Welcome: %s - lets get you chatting\n\n", profile.username);
}

int main()
{
    int socketfd = -1;
    // get input to send to server
    char user_input[MESSAGE_SIZE];
    FILE* sin = stdin;
    int send_res = 0;

    setup_user(sin);

    // new client message process
    // generate message header struct
    MessageHeader header = {0};
    header.message_type = C2S_NEW_CLIENT;
    header.payload_length = strlen(profile.username);
    header.sequence_number = ++profile.sequence_num;
    header.version = PROTOCOL_VERSION;

    // generate message payload struct
    MessagePayload payload = {0};
    strcpy(payload.new_client.username, profile.username);

    // pack in generic message
    Message msg = {0};
    msg.header = header;
    msg.payload = payload;

    // call serialize which will serialize both header and payload
    serialize(&msg);

    socketfd = client_init(socketfd);

    //   send_res = send(socketfd, profile.username, strlen(profile.username), 0);
    send_res = send(socketfd, &msg, HEADER_SIZE + sizeof(profile.username), 0);
    if (send_res < 0)
    {
        perror("Error sending msg to server");
    }

    printf("Msg sent: %i bytes sent\n", send_res);

    /*
    while (1)
    {
        // get input & remove newline
        fgets(user_input, MESSAGE_SIZE, sin);
        user_input[strcspn(user_input, "\n")] = 0;

        // send message to server
        send_res = send(socketfd, user_input, sizeof(user_input), 0);
    if (send_res < 0)
        {
            perror("Error sending msg to server");
            break;
        }

        printf("Msg sent: %s\n", user_input);
    }
    */

    // close client socket when done
    close(socketfd);
    printf("Socket closed\n");

    return 0;
}
