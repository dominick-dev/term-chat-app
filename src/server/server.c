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
static void add_new_client(int socket_fd, const struct sockaddr_in* client_addr, ClientState* curr_client)
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

    client_states[curr_nfds_idx].recv_state.ActiveState = WAITING;

    // assign client an id
    curr_client->clientId = next_client_id++;

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
    if (client_states[*i].recv_state.pay_buff != NULL)
    {
        free(client_states[*i].recv_state.pay_buff);
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
    Message msg = {0};
    msg.header.message_type = S2C_ROOM_LIST;
    msg.header.client_id = curr_client->clientId;
    msg.header.version = PROTOCOL_VERSION;

    uint8_t count = 0;

    for (int j = 0; j < MAX_ROOMS; j++)
    {
        ServerRoom curr_room = rooms[j];
        if (!curr_room.isActive)
        {
            break;
        }

        // fill msg payload
        msg.payload.room_list.rooms[j].server_id = curr_room.server_id;
        memcpy(msg.payload.room_list.rooms[j].server_name, curr_room.server_name, sizeof(uint8_t) * 21);

        count++;
    }

    if (count == 0)
    {
        printf("Sending room msg to client\n");
        msg.header.payload_length = 0;
    }
    else
    {
        // package up and send active rooms, also include option to create room?
        printf("Send active rooms message\n");
        msg.header.payload_length = (ROOM_INFO_SIZE * count) + 1;
        msg.payload.room_list.num_active_rooms = count;
    }

    uint8_t* buff = malloc(sizeof(msg));

    serialize(&msg, buff);

    int send_res = send(pfds[i].fd, buff, HEADER_SIZE + (ROOM_INFO_SIZE * count) + msg.header.payload_length, 0);

    if (send_res < 0)
    {
        printf("Error sending msg to client\n");
    }

    free(buff);
    printf("Msg sent: %i bytes sent\n", send_res);

    // on C2S_JOIN_ROOM req (contains room id to join) add client to that room
    // or on C2S_CREATE_ROOM req (contains room to make), create room and add client to that room
    // after adding, send msg to client w/ room joined id chat history of that room (if not a new room)
}

static void handle_new_client(Message* msg, ClientState* curr_client, int i)
{
    // TODO: more logic here to handle sending rooms vs adding new room?
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
        printf("Procesing new client msg\n");
        handle_new_client(msg, curr_client, i);
        break;
    default:
        printf("Unrecognized message type: %d\n", msg->header.message_type);
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
                add_new_client(socket_fd, client_addr, &client_states[curr_nfds_idx]);
                continue;
            }

            // POLLIN -> client
            if ((curr_fd.fd != socket_fd) && (curr_fd.revents & POLLIN))
            {
                Message msg = {0};
                ClientState* curr_client = &client_states[i];

                printf("Receiving message from client\n");
                int result = recv_message(pfds[i].fd, &curr_client->recv_state, &msg);
                if (result == -1)
                {
                    handle_client_leave(&i);
                }
                else if (result == 1)
                {
                    route_client_message(&msg, curr_client, i);
                }

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
