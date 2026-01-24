#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../include/logger.h"

#define PORT 8080
#define MAX_CLIENTS 100 // does not include listening socket
#define MESSAGE_SIZE 256

static uint16_t curr_nfds_idx = 0;

/*
 * Initializes the server socket (creates server socket, forces socket address, binds, listens)
 * Returns listening socket fd
 */
static int server_init()
{
    LOG_INFO(__FUNCTION__, "initializing the server...");

    int socket_fd, sock_opt;
    int yes = 1;
    struct sockaddr_in serv_addr;

    // setup socket
    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // force socket to attach to port
    sock_opt = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (sock_opt != 0)
    {
        perror("Error setting sock options");
    }

    // init serv_addr struct
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // bind socket to ip and port
    int server = bind(socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (server < 0)
    {
        perror("Error binding server socket");
        exit(EXIT_FAILURE);
    }

    // listen for client connection
    if (listen(socket_fd, 5) != 0)
    {
        perror("Error listening on server socket");
        exit(EXIT_FAILURE);
    }

    LOG_INFO(__FUNCTION__, "server initialized");

    return socket_fd;
}

/*
 * Helper funciton to visualize pfds, prints and logs same message
 */
static void print_pfds(struct pollfd pfds[], int pfds_size)
{
    for (int i = 0; i < pfds_size; i++)
    {
        struct pollfd curr = pfds[i];
        LOG_DEBUG(__FUNCTION__, "fd at position %i: %i\n", i, curr.fd);
        printf("fd at position %i: %i\n", i, curr.fd);
    }
}

/*
 * Accepts new client and adds client socket to pfds
 */
static void add_new_client(int socket_fd, int* new_socket, struct pollfd* pfds, const struct sockaddr_in* client_addr)
{
    socklen_t client_socket_len = sizeof(*client_addr);

    // check that we don't exceed MAX_CLIENTS
    if (curr_nfds_idx >= (1 + MAX_CLIENTS))
    {
        printf("Max clients accepted, cannot add\n");
        // still accept to remove from poll
        *new_socket = accept(socket_fd, (struct sockaddr*)client_addr, &client_socket_len);
        return;
    }

    // accept and add to pfds
    *new_socket = accept(socket_fd, (struct sockaddr*)client_addr, &client_socket_len);
    if (*new_socket == -1)
    {
        perror("Error accepting new client");
        return;
    }

    // add new socket to pfds
    pfds[curr_nfds_idx].fd = *new_socket;
    pfds[curr_nfds_idx].events = POLLIN;
    pfds[curr_nfds_idx].revents = 0;

    // log this in future rather than print
    printf("New client connection: %i\n", *new_socket);
    curr_nfds_idx++;
}

/*
 * Helper to remove client from pfds and manage pointers
 */
static void handle_client_leave(struct pollfd* pfds, int* i)
{
    close(pfds[*i].fd);
    curr_nfds_idx--;
    pfds[*i] = pfds[curr_nfds_idx];
    (*i)--;
}

/*
 * Recieves msg from existing client
 */
static void recv_client(struct pollfd* pfds, int* i)
{
    char* buff = malloc(MESSAGE_SIZE * sizeof(char));
    int bytes = recv(pfds[*i].fd, buff, MESSAGE_SIZE, 0);
    if (bytes < 0)
    {
        perror("Error receiving message");
    }

    // client orderly shutdown
    if (bytes == 0)
    {
        printf("Recv 0 bytes from client (%i), orderly shutdown\n", pfds[*i].fd);

        // remove client and clean up
        handle_client_leave(pfds, i);
        free(buff);
        return;
    }

    // will need to make sure all is recv
    printf("Message received from client %i: %s\n", pfds[*i].fd, buff);
    free(buff);
}

int main()
{
    logger_init("dev.log", LOG_DEBUG);

    // init socket vars
    int socket_fd = server_init();

    printf("listening socket_fd: %i\n", socket_fd);

    struct sockaddr_in client_addr;
    int new_socket;

    printf("Server listening on port %i\n", PORT);

    // init pollfd array
    struct pollfd pfds[MAX_CLIENTS] = {0};

    // init pfds w/ lisetening server
    pfds[0].fd = socket_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    curr_nfds_idx++;
    int num_polled = 0;

    // main program flow loop
    while (1)
    {
        // would cause poll() to block forever
        if (curr_nfds_idx < 1)
        {
            perror("ERROR! Too few in fds");
            exit(EXIT_FAILURE);
        }

        // check poll return value
        num_polled = poll(pfds, curr_nfds_idx, -1);

        if (num_polled < 0)
        {
            perror("Error polling for new events");
        }
        else if (num_polled == 0) // shouldn't ever enter
        {
            printf("Nothing polled\n");
            continue;
        }

        // events returned, iterate through pfds
        for (int i = 0; i < curr_nfds_idx; i++)
        {
            const struct pollfd curr_fd = pfds[i];

            // skip sockets w/ no new events
            if (curr_fd.revents == 0)
            {
                continue;
            }

            // POLLIN -> server
            if ((curr_fd.fd == socket_fd) && (curr_fd.revents & POLLIN))
            {
                add_new_client(socket_fd, &new_socket, pfds, &client_addr);
                continue;
            }

            // POLLIN -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLIN))
            {
                recv_client(pfds, &i);
                continue;
            }

            // POLLHUP -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLHUP))
            {
                printf("Client hung up\n");
                // remove from pollfd
                handle_client_leave(pfds, &i);
                continue;
            }

            // POLLERR -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLERR))
            {
                printf("Existing client error\n");
                // remove from pollfd
                handle_client_leave(pfds, &i);
                continue;
            }
        }
    }

    // close server socket when done
    close(socket_fd);
    close(new_socket);

    return 0;
}
