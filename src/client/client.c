#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../include/logger.h"

#define PORT 8080
#define MESSAGE_SIZE 256

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

int main()
{
    int socketfd = -1;

    socketfd = client_init(socketfd);

    // get input to send to server
    char user_input[MESSAGE_SIZE];
    FILE* sin = stdin;
    int send_res = 0;

    while (1)
    {
        // get input & remove
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

    // close client socket when done
    close(socketfd);
    printf("Socket closed\n");

    return 0;
}
