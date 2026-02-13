#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <poll.h>
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
#define MAX_CLIENTS 100 // does not include listening socket
#define MESSAGE_SIZE 255

typedef struct
{
    // header state
    uint8_t head_buff[HEADER_SIZE];
    size_t head_bytes_recv;

    // payload state
    uint8_t* pay_buff;
    size_t pay_expected_bytes;
    size_t pay_bytes_recv;

    // active client state
    enum
    {
        EMPTY,          // on init
        WAITING,        // initialized, no current msg being processed
        READING_HEADER, // header being read
        READING_PAYLOAD // payload being read
    } ActiveState;
} ClientState;

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
static void add_new_client(int socket_fd, struct pollfd* pfds, const struct sockaddr_in* client_addr, ClientState* client_states)
{
    socklen_t client_socket_len = sizeof(*client_addr);
    int new_socket = -1;

    // check that we don't exceed MAX_CLIENTS
    if (curr_nfds_idx >= (1 + MAX_CLIENTS))
    {
        printf("Max clients accepted, cannot add\n");
        // still accept to remove from poll
        new_socket = accept(socket_fd, (struct sockaddr*)client_addr, &client_socket_len);
        return;
    }

    // accept and add to pfds
    new_socket = accept(socket_fd, (struct sockaddr*)client_addr, &client_socket_len);
    if (new_socket == -1)
    {
        perror("Error accepting new client");
        return;
    }

    // add new socket to pfds & client_states
    pfds[curr_nfds_idx].fd = new_socket;
    pfds[curr_nfds_idx].events = POLLIN;
    pfds[curr_nfds_idx].revents = 0;
    client_states[curr_nfds_idx].ActiveState = WAITING;

    // log this in future rather than print
    printf("New client connection: %i\n", new_socket);
    curr_nfds_idx++;
}

/*
 * Helper to remove client from pfds and manage pointers
 */
static void handle_client_leave(struct pollfd* pfds, int* i, ClientState* client_states)
{
    // close socket, manage pfds & client_state, decrement i in caller
    close(pfds[*i].fd);
    curr_nfds_idx--;
    pfds[*i] = pfds[curr_nfds_idx];
    client_states[*i] = client_states[curr_nfds_idx];
    (*i)--;
}

/*
 * Recieves msg from existing client
 */
static void recv_client(struct pollfd* pfds, int* i, ClientState* client_states)
{
    printf("recv_client\n");

    ClientState* curr = &client_states[*i];

    uint8_t* header_buff = NULL;
    uint8_t* payload_buff = NULL;
    size_t bytes = 0;

    // switch on possible client state
    switch (curr->ActiveState)
    {
    case (EMPTY):
        perror("Recv_client with empty client state, something is wrong");
        break;
    case (WAITING):
        header_buff = malloc(HEADER_SIZE * sizeof(uint8_t));
        bytes = recv(pfds[*i].fd, header_buff, HEADER_SIZE, 0);

        // client leave, no msg to parse
        if (bytes == 0)
        {
            printf("Recv 0 bytes from client (%i), orderly shutdown\n", pfds[*i].fd);

            // remove client and clean up
            handle_client_leave(pfds, i, client_states);
            free(header_buff);
            return;
        }

        // set active state
        if (bytes == HEADER_SIZE)
        {
            curr->ActiveState = READING_PAYLOAD;
            printf("Read full header on first recv\n");
        }
        else
        {
            curr->ActiveState = READING_HEADER;
            printf("Still reading header after first recv\n");
        }

        // copy buff over to client state & free temp buff
        memcpy(curr->head_buff, header_buff, HEADER_SIZE);
        curr->head_bytes_recv += bytes;
        free(header_buff);

        break;
    case (READING_HEADER):
        // continue reading header, change active state if needed
        header_buff = malloc(HEADER_SIZE * sizeof(uint8_t));
        bytes = recv(pfds[*i].fd, header_buff, HEADER_SIZE - curr->head_bytes_recv, 0);

        // set active state
        if (bytes + curr->pay_bytes_recv == HEADER_SIZE)
        {
            curr->ActiveState = READING_PAYLOAD;
            printf("Read full header in READING_HEADER\n");
        }
        else
        {
            printf("Still reading header in READING_HEADER\n");
        }

        memcpy(curr->head_buff + curr->head_bytes_recv, header_buff, bytes);
        curr->head_bytes_recv += bytes;
        free(header_buff);

        break;
    case (READING_PAYLOAD):;
        uint32_t payload_len = -1;
        memcpy(&payload_len, curr->head_buff + sizeof(uint8_t), sizeof(uint32_t));
        printf("Payload length: %i\n", htonl(payload_len));

        payload_buff = malloc(payload_len * sizeof(uint8_t));
        bytes = recv(pfds[*i].fd, payload_buff + curr->pay_bytes_recv, sizeof(payload_len) - curr->pay_bytes_recv, 0);

        if (bytes + curr->pay_bytes_recv == sizeof(payload_len))
        {
            printf("Full payload received\n");
            curr->ActiveState = WAITING;
            // deserialize message, still need to copy buff, add bytes recv, and free though
            Message msg = {0};
            deserialize_header(header_buff, &msg);
            print_header(&msg);
        }
        else
        {
            printf("More bytes to read: %lu", (sizeof(payload_len) - bytes - curr->pay_bytes_recv));
        }

        memcpy(curr->pay_buff + curr->head_bytes_recv, payload_buff, bytes);
        curr->pay_bytes_recv += bytes;
        free(payload_buff);

        break;
    default:
        printf("Invalid messge type\n");
    }
}

void run_server(struct pollfd* pfds, int socket_fd,
                struct sockaddr_in* client_addr, ClientState* client_states)
{
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
                add_new_client(socket_fd, pfds, client_addr, client_states);
                continue;
            }

            // POLLIN -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLIN))
            {
                // recv_client(pfds, &i, client_states);
                printf("Recv-ing client\n");

                uint8_t* header_buff = malloc(HEADER_SIZE * sizeof(uint8_t));
                int bytes = recv(pfds[i].fd, header_buff, 100, 0);

                if (bytes == 0)
                {
                    handle_client_leave(pfds, &i, client_states);
                }
                printf("%i\n", bytes);

                free(header_buff);

                continue;
            }

            // POLLHUP -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLHUP))
            {
                printf("Client hung up\n");
                // remove from pollfd
                handle_client_leave(pfds, &i, client_states);
                continue;
            }

            // POLLERR -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLERR))
            {
                printf("Existing client error\n");
                // remove from pollfd
                handle_client_leave(pfds, &i, client_states);
                continue;
            }
        }
    }
}

int main()
{
    // need something to map client socket fd to client username
    // if duplicate username just do username(2) as username and so on

    // init logger
    logger_init("dev.log", LOG_DEBUG);

    // init socket vars
    int socket_fd = server_init();
    struct sockaddr_in client_addr;

    printf("Server listening on port %i\n", PORT);

    struct pollfd pfds[MAX_CLIENTS] = {0};
    ClientState client_states[MAX_CLIENTS] = {0};

    // add lisetening server to pfds
    pfds[0].fd = socket_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    curr_nfds_idx++;

    // main program flow loop
    run_server(pfds, socket_fd, &client_addr, client_states);

    // close server socket when done
    close(socket_fd);

    return 0;
}
