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

#define PORT 8080
#define MAX_CLIENTS 1000
#define MESSAGE_SIZE 256

static uint16_t curr_nfds_idx = 0;

/*
 * Initializes the server socket
 * Creates server socket, forces socket address, binds, and listens
 * Error checks socket related calls
 * Returns listening socket fd
 */
int server_init()
{
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

    // TODO: move these func calls inside if conditional for cleaner code?
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

    return socket_fd;
}

/*
 * Helper funciton to visualize pfds
 */
void print_pfds(struct pollfd pfds[], int pfds_size)
{
    for (int i = 0; i < pfds_size; i++)
    {
        struct pollfd curr = pfds[i];
        printf("fd at position %i: %i\n", i, curr.fd);
    }
}

/*
 * Accepts new client and adds client socket to pfds
 * Error checking for exceeding MAX_CLIENTS and accept()
 * Returns nothing
 */
void add_new_client(int socket_fd, int* new_socket, struct pollfd* pfds, const struct sockaddr_in* client_addr)
{
    // check that we don't exceed MAX_CLIENTS
    if (curr_nfds_idx >= MAX_CLIENTS)
    {
        perror("Max clients accepted, cannot add");
        return;
    }

    socklen_t client_socket_len = sizeof(*client_addr);
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

int main()
{
    // init socket vars
    int socket_fd = server_init();

    printf("listening socket_fd: %i\n", socket_fd);

    struct sockaddr_in client_addr;
    int new_socket;
    char buff[256] = {0};

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
            struct pollfd currfd = pfds[i];

            // skip sockets w/ no new events
            if (currfd.revents == 0)
            {
                continue;
            }

            // new client connection
            if ((currfd.fd == socket_fd) && (currfd.revents & POLLIN))
            {
                add_new_client(socket_fd, &new_socket, pfds, &client_addr);
            }

            // new message from existing client
            if ((currfd.fd != socket_fd) && (currfd.revents & POLLIN))
            {
                // recv logic, routing later
                int bytes = recv(currfd.fd, buff, MESSAGE_SIZE, MSG_WAITALL);
                if (bytes < 0)
                {
                    perror("Error receiving message");
                }

                // client orderly shutdown
                if (bytes == 0)
                {
                    printf("Recv 0 bytes from client (%i), orderly shutdown\n", currfd.fd);
                    close(currfd.fd);
                    currfd = pfds[curr_nfds_idx - 1];
                    curr_nfds_idx--;
                    i--;
                    continue;
                }

                // will need to make sure all is recv
                printf("Message received from client %i: %s\n", currfd.fd, buff);
            }
            /*
// existing client disconnects (gracefully or w/ error)
if ((currfd.fd != socket_fd) &&
    (currfd.revents & (POLLERR | POLLHUP)))
{
    if (currfd.revents & POLLHUP)
    {
        printf("Client gracefully disconnected\b");
    }
    else
    {
        printf("Client connection error: %i\n", currfd.fd);
    }

    // swap idx being removed and last idx that holds data
    pfds[i] = pfds[curr_nfds_idx - 1];
    curr_nfds_idx--;
}
 */
        }
    }

    // 6. close server socket when done
    close(socket_fd);
    close(new_socket);

    return 0;
}
