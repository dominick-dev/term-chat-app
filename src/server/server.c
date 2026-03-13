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
static uint16_t next_server_id = 1;

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
void print_pfds(int pfds_size)
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
    if (curr_nfds_idx >= MAX_CLIENTS)
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

    curr_client->recv_state.ActiveState = WAITING;
    curr_client->clientId = next_client_id++;
    curr_client->socketfd = new_socket;

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

    // TODO: send new msg type to notify room that someone has left the chat

    // manage client's entry in rooms array
    // find room client is in
    for (int j = 0; j < MAX_ROOMS; j++)
    {
        if (rooms[j].server_id == client_states[*i].joined_server_id)
        {
            ServerRoom* curr_room = &rooms[j];
            for (int k = 0; k < MAX_CLIENTS; k++)
            {
                uint32_t curr_client_id = curr_room->joined_client_ids[k];
                if (curr_client_id == client_states[*i].clientId)
                {
                    curr_room->num_clients_in_room--;
                    curr_room->joined_client_ids[k] = curr_room->joined_client_ids[curr_room->num_clients_in_room];
                    curr_room->joined_client_ids[curr_room->num_clients_in_room] = 0;
                    break;
                }
            }
        }
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

static void handle_join_room(Message* msg, ClientState* curr_client, ServerRoom* room)
{
    // TODO: need to send joined message back to client
    //  forget sending chat history for now, just let them know they were added
    //  also need to work on this logic below, pretty fragile as is
    Message res_msg = {0};
    res_msg.header.client_id = curr_client->clientId;
    res_msg.header.version = PROTOCOL_VERSION;
    res_msg.header.message_type = S2C_JOIN_ROOM_RES;
    res_msg.header.payload_length = sizeof(uint8_t) + (sizeof(uint16_t) + (21 * sizeof(uint8_t)));

    res_msg.payload.join_room_res.joined_result = false; // initial assumption

    // newly created room
    if (room != NULL)
    {
        room->joined_client_ids[0] = curr_client->clientId;
        curr_client->joined_server_id = room->server_id;
        room->num_clients_in_room++;

        // populate payload
        res_msg.payload.join_room_res.joined_result = true;
        res_msg.payload.join_room_res.joined_room.server_id = room->server_id;
        memcpy(res_msg.payload.join_room_res.joined_room.server_name, room->server_name, (21 * sizeof(uint8_t)));
    }
    else
    {
        // find existing room by id from message
        for (int i = 0; i < MAX_ROOMS; i++)
        {
            if (rooms[i].server_id == msg->payload.join_room.room_to_join.server_id)
            {
                printf("Found requested server\n");
                // add client to joined_client_ids
                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    // find next empty spot in server
                    if (rooms[i].joined_client_ids[j] == 0)
                    {
                        rooms[i].joined_client_ids[j] = curr_client->clientId;
                        curr_client->joined_server_id = rooms[i].server_id;
                        rooms[i].num_clients_in_room++;

                        // fill payload
                        res_msg.payload.join_room_res.joined_result = true;
                        res_msg.payload.join_room_res.joined_room.server_id = rooms[i].server_id;
                        memcpy(res_msg.payload.join_room_res.joined_room.server_name, rooms[i].server_name, (21 * sizeof(uint8_t)));

                        goto found_and_added;
                    }
                }
                // TODO: handle this case better
                printf("No more space in room for client\n");
                return;
            }
        }

        // TODO: handle this case better
        printf("Could not find room\n");
        return;
    }

found_and_added:;
    // serialize and send message
    uint8_t* buff = malloc(sizeof(res_msg));
    serialize(&res_msg, buff);

    int send_res = send(curr_client->socketfd, buff, HEADER_SIZE + res_msg.header.payload_length, 0);

    if (send_res < 0)
    {
        printf("Error sending msg to client\n");
    }

    free(buff);
}

static void handle_create_new_server_room(Message* msg, ClientState* curr_client)
{
    // find next open spot
    for (int j = 0; j < MAX_ROOMS; j++)
    {
        if (rooms[j].isActive == false)
        {
            ServerRoom* curr_room = &rooms[j];

            // create new room
            curr_room->isActive = true;
            curr_room->server_id = next_server_id++;
            memcpy(curr_room->server_name, msg->payload.create_room.server_name, msg->header.payload_length);

            printf("Created room \"%s\" with id %i\n", curr_room->server_name, curr_room->server_id);

            // add curr client to room and send message back to client
            handle_join_room(msg, curr_client, curr_room);

            return;
        }
    }

    // TODO: make generic failure message to send to client here
    // should include function that failed and message
    // client receives this and prints it
    printf("No more rooms!\n");
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
            continue;
        }

        // fill msg payload
        msg.payload.room_list.rooms[count].server_id = curr_room.server_id;
        memcpy(msg.payload.room_list.rooms[count].server_name, curr_room.server_name, sizeof(uint8_t) * 21);

        count++;
    }

    if (count == 0)
    {
        msg.header.payload_length = 0;
    }
    else
    {
        // package up and send active rooms, also include option to create room?
        msg.header.payload_length = (ROOM_INFO_SIZE * count) + 1;
        msg.payload.room_list.num_active_rooms = count;
    }

    uint8_t* buff = malloc(sizeof(msg));
    serialize(&msg, buff);

    int send_res = send(pfds[i].fd, buff, HEADER_SIZE + msg.header.payload_length, 0);

    if (send_res < 0)
    {
        printf("Error sending msg to client\n");
    }

    free(buff);
}

static void handle_new_client(ClientState* curr_client, int i)
{
    // call send rooms function
    send_rooms(curr_client, i);
}

static void send_msg(Message* msg, int socketfd)
{
    // payload contains message and sender
    Message broadcast_msg = {0};
    broadcast_msg.header.message_type = S2C_BROADCAST_MSG;
    broadcast_msg.header.client_id = msg->header.client_id;
    broadcast_msg.header.sequence_number = 0; // TODO: correctly implement seq numbers
    broadcast_msg.header.version = PROTOCOL_VERSION;
    broadcast_msg.header.payload_length = msg->header.payload_length;

    memcpy(&broadcast_msg.payload, &msg->payload, sizeof(msg->payload));

    // TODO: package this up in a function
    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&broadcast_msg, buff);
    int send_res = send(socketfd, buff, HEADER_SIZE + broadcast_msg.header.payload_length, 0);
    if (send_res < 0)
    {
        printf("Error sending message to the server\n");
    }

    free(buff);
}

static void handle_new_message(Message* msg, ClientState* curr_client)
{
    // verify message was received by expected client
    // TODO: handle this somehow
    if (msg->header.client_id != curr_client->clientId)
    {
        printf("Inconsistency between server client id and sender id\n");
    }

    // get target room from msg
    RoomInfo target_room = msg->payload.new_msg.target_room;

    // iterate through each active chat room
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        // found target room
        if (rooms[i].server_id == target_room.server_id)
        {
            // iterate through each client in target room
            for (int j = 0; j < rooms[i].num_clients_in_room; j++)
            {
                // only proceed if curr client is not msg sender
                if (rooms[i].joined_client_ids[j] != curr_client->clientId)
                {
                    // iterate through active client states (can make this a helper)
                    for (int k = 0; k < MAX_CLIENTS; k++)
                    {
                        // found target client state
                        if (client_states[k].clientId == rooms[i].joined_client_ids[j])
                        {
                            printf("Sending msg to %i\n", rooms[i].joined_client_ids[j]);
                            send_msg(msg, client_states[k].socketfd);
                            break;
                        }
                    }
                }
            }

            // found room and sent messages, done
            break;
        }
    }
}

/*
 * Routes general client message to specific handler based on message type
 */
static void route_client_message(Message* msg, ClientState* curr_client, int i)
{
    switch (msg->header.message_type)
    {
    case C2S_NEW_CLIENT:
        handle_new_client(curr_client, i);
        break;
    case C2S_CREATE_ROOM:
        handle_create_new_server_room(msg, curr_client);
        break;
    case C2S_JOIN_ROOM:
        handle_join_room(msg, curr_client, NULL);
        break;
    case C2S_NEW_MSG:
        handle_new_message(msg, curr_client);
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
