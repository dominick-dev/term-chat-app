#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080

/*
 * Initializes the server socket
 */
int server_init()
{
    int socketfd;
    struct sockaddr_in serv_addr;

    // setup socket
    socketfd = socket(PF_INET, SOCK_STREAM, 0);
    if (socketfd < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // init serv_addr struct
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // bind socket to ip and port
    int server = bind(socketfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (server < 0)
    {
        perror("Error binding server socket");
        exit(EXIT_FAILURE);
    }

    // listen for client connection
    if (listen(socketfd, 5) != 0)
    {
        perror("Error listening on server socket");
        exit(EXIT_FAILURE);
    }

    return socketfd;
}

/*
 * Entry point for a newly connected client
 */
void client_channel()
{
    // init new client data
    // do chat
}

int main()
{
    // init socket vars
    int socketfd = server_init();
    struct sockaddr_in client_addr;
    socklen_t serv_socket_length = sizeof(socketfd);
    int new_socket;

    printf("Server listening on port %i\n", PORT);

    // accept new client connection
    while (1)
    {
        new_socket = accept(socketfd, (struct sockaddr*)&client_addr, &serv_socket_length);
        if (new_socket == -1)
        {
            // prob don't exit here, ignore error and continue
            // client will see connection failed and can try again
            // maybe log client connection failed w/ a logger?
            printf("Error adding client\n");
        }
        // handle new client connection...
        printf("Server socket connected to new client - %i\n", new_socket);
        break;
    }

    // 6. close server socket when done
    close(socketfd);
    close(new_socket);

    return 0;
}
