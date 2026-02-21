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
#include "server.h"

static uint16_t curr_nfds_idx = 0;
static uint32_t next_client_id = 1;

static struct pollfd pfds[MAX_CLIENTS] = {0};
static ClientState client_states[MAX_CLIENTS] = {0};
static ServerRoom rooms[MAX_ROOMS] = {0};

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
static void print_pfds(int pfds_size)
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
static void add_new_client(int socket_fd, const struct sockaddr_in* client_addr)
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

    client_states[next_client_id].ActiveState = WAITING;

    // log this in future rather than print
    printf("New client connection: %i\n", new_socket);
    curr_nfds_idx++;
}

/*
 * Helper to remove client from pfds and manage pointers
 */
static void handle_client_leave(int* i)
{
    // free buff, saftey net
    if (client_states[*i].pay_buff != NULL)
    {
        free(client_states[*i].pay_buff);
    }

    // reset current client state
    memset(&client_states[*i], 0, sizeof(ClientState));

    // close socket, manage pfds & client_state, decrement i in caller
    close(pfds[*i].fd);
    curr_nfds_idx--;
    pfds[*i] = pfds[curr_nfds_idx];
    client_states[*i] = client_states[curr_nfds_idx];

    // zero out now unused client state
    memset(&client_states[curr_nfds_idx], 0, sizeof(ClientState));

    (*i)--;
}

static void create_new_server_room()
{
    // iterate through rooms, find next open spot
    // if none availabe handle this
    //
}

static void send_rooms(ClientState* curr_client, int i)
{
    // iterate trhough rooms, add all acives ones to list
    ServerRoom active_rooms[MAX_ROOMS];
    int count = 0;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        ServerRoom curr_room = rooms[i];
        if (!curr_room.isActive)
        {
            break;
        }
        active_rooms[count] = rooms[i];
        count++;
    }

    if (count == 0)
    {
        // send create room msg to client
        // remember to include their client id in the message
        // create msg to be sent
        // send message and done?
        Message msg = {0};
        msg.header.message_type = S2C_CREATE_ROOM;
        msg.header.payload_length = 0;
        msg.header.client_id = curr_client->clientId;
        msg.header.version = PROTOCOL_VERSION;

        uint8_t* buff = malloc(sizeof(msg));
        serialize(&msg, buff);

        int send_res = send(pfds[i].fd, buff, MESSAGE_SIZE, 0);

        if (send_res < 0)
        {
            printf("Error sending msg to client\n");
        }
        free(buff);
        printf("Msg sent: %i bytes sent\n", send_res);
    }
    else
    {
        // package up and send active rooms, also include option to create room?
        // TODO: remember to include their client id in the message
        printf("Send active rooms message\n");
    }

    // on C2S_JOIN_ROOM req (contains room to join) add client to that room
    // or on C2S_CREATE_ROOM req (contains room to make), create room and add client to that room
    // after adding, send msg to client w/ chat history of that room (if not a new room)
}

static void handle_new_client(Message* msg, ClientState* curr_client, int i)
{
    // assign curr client an id, store server side
    curr_client->clientId = next_client_id++;
    // store username server side
    memcpy(curr_client->username, msg->payload.new_client.username, msg->header.payload_length);
    // call send rooms function
    send_rooms(curr_client, i);
}

/*
 * Routes general client message to specific handler based on message type
 */
static void route_client_message(Message* msg, ClientState* curr_client, int i)
{
    switch (msg->header.message_type)
    {
    case C2S_NEW_CLIENT:
        handle_new_client(msg, curr_client, i);
        break;
    default:
        printf("Unrecognized message type: %d\n", msg->header.message_type);
    }
}

/*
 * Recieves msg from existing client
 */
static void recv_client(int* i)
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
        // read header
        header_buff = calloc(HEADER_SIZE, sizeof(uint8_t));
        bytes = recv(pfds[*i].fd, header_buff, HEADER_SIZE, 0);

        // client leave, no msg to parse
        if (bytes == 0)
        {
            printf("Recv 0 bytes from client (%i), orderly shutdown\n", pfds[*i].fd);

            // remove client and clean up
            curr->ActiveState = EMPTY; // is this necessary?
            handle_client_leave(i);
            free(header_buff);
            return;
        }

        printf("Bytes waiting: %lu\n", bytes);

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
        header_buff = calloc(HEADER_SIZE, sizeof(uint8_t));
        bytes = recv(pfds[*i].fd, header_buff, HEADER_SIZE - curr->head_bytes_recv, 0);

        printf("Bytes reading header: %lu\n", bytes);

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
    case (READING_PAYLOAD):
        // first time reading payload
        if (curr->pay_expected_bytes == 0)
        {
            memcpy(&curr->pay_expected_bytes, curr->head_buff + sizeof(uint8_t), sizeof(uint32_t));
            printf("Payload length: %u\n", htonl(curr->pay_expected_bytes));
            curr->pay_buff = calloc(curr->pay_expected_bytes, sizeof(uint8_t));
        }

        uint32_t payload_len = htonl(curr->pay_expected_bytes);

        payload_buff = calloc(payload_len, sizeof(uint8_t));
        bytes = recv(pfds[*i].fd, payload_buff + curr->pay_bytes_recv, payload_len - curr->pay_bytes_recv, 0);

        printf("Bytes reading payload: %lu\n", bytes);

        if (bytes + curr->pay_bytes_recv == payload_len)
        {
            printf("Full payload received\n");
            curr->ActiveState = WAITING;

            memcpy(curr->pay_buff + curr->pay_bytes_recv, payload_buff, bytes);
            free(payload_buff);

            // deserialize message
            show_buff_hex(curr->pay_buff, 20);
            Message msg = {0};
            deserialize_header(curr->head_buff, &msg);
            deserialize_payload(curr->pay_buff, &msg);

            route_client_message(&msg, curr, *i);

            break;
        }
        else
        {
            printf("More bytes to read: %lu\n", (sizeof(payload_len) - bytes - curr->pay_bytes_recv));
        }

        memcpy(curr->pay_buff + curr->pay_bytes_recv, payload_buff, bytes);
        curr->pay_bytes_recv += bytes;
        free(payload_buff);

        break;
    default:
        printf("Invalid messge type\n");
    }
}

void run_server(int socket_fd, struct sockaddr_in* client_addr)
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
                add_new_client(socket_fd, client_addr);
                continue;
            }

            // POLLIN -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLIN))
            {
                recv_client(&i);
                continue;
            }

            // POLLHUP -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLHUP))
            {
                printf("Client hung up\n");
                // remove from pollfd
                handle_client_leave(&i);
                continue;
            }

            // POLLERR -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLERR))
            {
                printf("Existing client error\n");
                // remove from pollfd
                handle_client_leave(&i);
                continue;
            }
        }
    }
}

int main()
{
    // init logger
    logger_init("dev.log", LOG_DEBUG);

    // init socket vars
    int socket_fd = server_init();
    struct sockaddr_in client_addr;

    printf("Server listening on port %i\n", PORT);

    // init status structs

    // add lisetening server to pfds
    pfds[0].fd = socket_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    curr_nfds_idx++;

    // main program flow loop
    run_server(socket_fd, &client_addr);

    // close server socket when done
    close(socket_fd);

    return 0;
}
