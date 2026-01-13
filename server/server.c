#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define MAX_CLIENTS 1000

static uint8_t curr_nfds_idx = 0;

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

int main()
{
    // init socket vars
    int socketfd = server_init();
    struct sockaddr_in client_addr;
    socklen_t serv_socket_length = sizeof(socketfd);
    int new_socket;

    printf("Server listening on port %i\n", PORT);

    // init poll
    struct pollfd pfds[MAX_CLIENTS];

    // init pfds w/ lisetening server
    pfds->fd = socketfd;
    pfds->events = POLLIN;
    pfds->revents = POLLIN;
    curr_nfds_idx++;
    int num_polled = 0;

    // TODO:
    // add logic in while loop for each socket ready event (server and client)
    // refactor

    while (1)
    {
        // check poll return value
        num_polled = poll(pfds, curr_nfds_idx, -1);
        if (num_polled < 0)
        {
            perror("Error polling for new events");
        }
        else if (num_polled == 0)
        {
            continue;
        }

        // events returned, iterate through pfds
        for (int i = 0; i < curr_nfds_idx; i++)
        {
            struct pollfd currfd = pfds[i];

            // new client connection
            if ((currfd.fd == socketfd) && (currfd.revents && POLLIN))
            {
                //
            }

            // new message from existing client
            if ((currfd.fd != socketfd) && (currfd.revents && POLLIN))
            {
                //
            }
        }

        // results, iterate through pfds and check for pollfd with events set?
        // depending on revents and socket type (client or server) handle each case

        // ready has a result!
        if (num_polled > 0)
        {
            new_socket = accept(socketfd, (struct sockaddr*)&client_addr, &serv_socket_length);

            if (new_socket == -1)
            {
                // maybe log client connection failed w/ a logger?
                printf("Error adding client\n");
            }

            // handle new client connection...
            printf("Server socket connected to new client - %i\n", new_socket);
        }
    }

    // 6. close server socket when done
    close(socketfd);
    close(new_socket);

    return 0;
}
