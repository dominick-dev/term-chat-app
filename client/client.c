#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define MESSAGE_LENGTH 256

/*
 * Initializes the client socket
 */
int client_init(int socketfd)
{
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
        exit(EXIT_FAILURE);
    }

    // attempt to connect to server socket
    int conn_res = connect(socketfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (conn_res != 0)
    {
        perror("Error connecting to server socket");
        exit(EXIT_FAILURE);
    }

    return socketfd;
}

int main()
{
    int socketfd = -1;
    size_t msg_size = 0;

    // init client connecting
    socketfd = client_init(socketfd);

    // get input to send to server
    char user_input[MESSAGE_LENGTH];
    FILE* sin = stdin;
    int send_res = 0;

    while (1)
    {
        // get input & remove
        fgets(user_input, MESSAGE_LENGTH, sin);
        user_input[strcspn(user_input, "\n")] = 0;
        msg_size = sizeof(user_input);

        // send message to server
        send(socketfd, user_input, msg_size, 0);
        if (send_res < 0)
        {
            perror("Error sending msg to server");
            break;
        }

        printf("Msg sent!\n");
    }

    // close client socket when done
    close(socketfd);
    printf("Socket closed\n");

    return 0;
}
