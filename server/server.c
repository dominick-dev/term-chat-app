#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8001

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
    else
    {
        printf("Server listening on port %i", PORT);
    }

    return socketfd;
}

int main()
{
    // init socket vars
    int socketfd = server_init();
    struct sockaddr_in client_addr;
    socklen_t serv_socket_length = sizeof(socketfd);
    /* SERVER STEPS */
    server_init();

    // 4. accept new client connection
    int new_socket = accept(socketfd, (struct sockaddr*)&client_addr, &serv_socket_length);
    if (new_socket == -1)
    {
        printf("Error connecting to new client socket: %s\n", strerror(errno));
    }
    else
    {
        printf("Server socket connected to new client!\n");
    }

    // 5. send/recieve data w/ client
    char buff[256] = {0};
    ssize_t recv_res = recv(new_socket, buff, 256, 0);
    if (recv_res < 0)
    {
        printf("Error receiving message from client: %s\n", strerror(errno));
    }
    else if (recv_res == 0)
    {
        printf("No messages available to be recieved and peer has performed an orderly shutdown.\n");
    }
    else
    {
        printf("Message from the client: %s\n", buff);
    }

    // send message back to client
    char serv_msg[] = "Hello from the server!\n";
    ssize_t snd_res = send(new_socket, serv_msg, sizeof(serv_msg), 0);
    if (snd_res == -1)
    {
        printf("Message send from Server failed: %s\n", strerror(errno));
    }

    // 6. close server socket when done
    close(socketfd);
    close(new_socket);
    return 0;
}
