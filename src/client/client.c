#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/logger.h"
#include "../../include/protocol.h"

#define PORT 8080
#define MAX_USERNAME_LEN 21   // includes \0
#define MAX_SERVERNAME_LEN 21 // includes \0

typedef enum
{
    AWAITING_ROOM_LIST,
    IN_ROOM_MENU,
    CREATING_ROOM,
    IN_ROOM
} ChatState;

typedef struct
{
    RecvState state;
    char username[21];
    uint32_t id;
    uint16_t sequence_num;
    ChatState chat_state;
    RoomInfo activeRoom;
    uint8_t num_active_rooms;
    RoomInfo rooms[MAX_ROOMS];
} ClientState;

static ClientState profile = {0};

#ifdef _WIN32
#include <process.h>

#define STDIN_QUEUE_SIZE 16
#define MAX_INPUT_LEN    512

typedef struct {
    char lines[STDIN_QUEUE_SIZE][MAX_INPUT_LEN];
    int  head;
    int  tail;
    int  count;
    CRITICAL_SECTION lock;
    HANDLE           data_ready; // manual-reset event
    HANDLE           hThread;
} StdinQueue;

static StdinQueue g_stdin_queue;

static unsigned __stdcall stdin_reader_thread(void *arg) {
    (void)arg;
    char buf[MAX_INPUT_LEN];
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        EnterCriticalSection(&g_stdin_queue.lock);
        if (g_stdin_queue.count < STDIN_QUEUE_SIZE) {
            strncpy(g_stdin_queue.lines[g_stdin_queue.tail], buf, MAX_INPUT_LEN - 1);
            g_stdin_queue.lines[g_stdin_queue.tail][MAX_INPUT_LEN - 1] = '\0';
            g_stdin_queue.tail = (g_stdin_queue.tail + 1) % STDIN_QUEUE_SIZE;
            g_stdin_queue.count++;
            SetEvent(g_stdin_queue.data_ready);
        }
        LeaveCriticalSection(&g_stdin_queue.lock);
    }
    return 0;
}

void init_stdin_queue(void) {
    memset(&g_stdin_queue, 0, sizeof(g_stdin_queue));
    InitializeCriticalSection(&g_stdin_queue.lock);
    g_stdin_queue.data_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_stdin_queue.hThread = (HANDLE)_beginthreadex(NULL, 0, stdin_reader_thread, NULL, 0, NULL);
}

int try_pop_stdin(char *out, int max_len) {
    int got = 0;
    EnterCriticalSection(&g_stdin_queue.lock);
    if (g_stdin_queue.count > 0) {
        strncpy(out, g_stdin_queue.lines[g_stdin_queue.head], max_len - 1);
        out[max_len - 1] = '\0';
        g_stdin_queue.head = (g_stdin_queue.head + 1) % STDIN_QUEUE_SIZE;
        g_stdin_queue.count--;
        if (g_stdin_queue.count == 0) ResetEvent(g_stdin_queue.data_ready);
        got = 1;
    }
    LeaveCriticalSection(&g_stdin_queue.lock);
    return got;
}

void cleanup_stdin_queue(void) {
    if (g_stdin_queue.hThread) {
        CloseHandle(g_stdin_queue.hThread);
        g_stdin_queue.hThread = NULL;
    }
    CloseHandle(g_stdin_queue.data_ready);
    DeleteCriticalSection(&g_stdin_queue.lock);
}
#endif

static const char* parse_args(int num_args, char** args_arr)
{
    if (num_args == 1)
    {
        printf("Connecting to local server\n");
        return "127.0.0.1";
    }
    else if (num_args == 2)
    {
        printf("Connecting to pi server\n");
        return args_arr[1];
    }
    else
    {
        printf("Usage: %s <server_ip>\n", args_arr[0]);
        exit(EXIT_FAILURE);
    }
}

static void poll_init(struct pollfd* pfds, SOCKET_T socketfd)
{
    profile.state.ActiveState = WAITING;

    // add server to pfds
    pfds[0].fd = socketfd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

#ifndef _WIN32
    // add stdin to pfds
    pfds[1].fd = STDIN_FILENO;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;
#endif
}

/*
 * Initializes the client socket
 */
static void socket_init(SOCKET_T* socketfd, const char* server_ip)
{
    LOG_INFO(__FUNCTION__, "initializing the client...\n");

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        exit(EXIT_FAILURE);
    }
#endif

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid server IP address");
#ifdef _WIN32
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }
    serv_addr.sin_port = htons(PORT);

    // create client socket
    *socketfd = socket(PF_INET, SOCK_STREAM, 0);
    if (*socketfd == INVALID_SOCKET_T)
    {
        PRINT_SOCKET_ERROR("Error creating client socket");
#ifdef _WIN32
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // attempt to connect to server socket
    int conn_res = connect(*socketfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (conn_res != 0)
    {
        PRINT_SOCKET_ERROR("Error connecting to server socket");
        CLOSE_SOCKET(*socketfd);
#ifdef _WIN32
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }

    // made it here, connected to server
    LOG_INFO(__FUNCTION__, "Connected to server");
    printf("Connected to server!\n");
}

/*
 * Intro client to program, initialize client profile
 */
static void setup_user(FILE* sin)
{
    printf("Welcome to the chat app!!\n");
    printf("First, please input your username\n");

    fgets(profile.username, MAX_USERNAME_LEN, sin);

    // if input longer than buff, discard rest
    if (strchr(profile.username, '\n') == NULL)
    {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            continue;
    }

    profile.username[strcspn(profile.username, "\n")] = 0;
    profile.sequence_num = 0;
    profile.chat_state = AWAITING_ROOM_LIST;

    printf("Welcome: %s - lets get you chatting\n\n", profile.username);
}

static void send_new_join(SOCKET_T socketfd)
{
    int send_res = -1;

    // generate message header struct
    MessageHeader header = {0};
    header.message_type = C2S_NEW_CLIENT;
    header.payload_length = NEW_CLIENT_MSG_PAYLOAD_SIZE;
    header.sequence_number = ++profile.sequence_num;
    header.version = PROTOCOL_VERSION;
    header.client_id = 0; // will be assigned by server

    // generate message payload struct
    NewClientMsgPayload payload = {0};
    memcpy(payload.username, profile.username, header.payload_length);

    // pack in generic message
    Message msg = {0};
    msg.header = header;
    msg.payload.new_client = payload;

    // serialize
    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);

    // send new client msg to server
    send_res = send(socketfd, (const char*)buff, HEADER_SIZE + NEW_CLIENT_MSG_PAYLOAD_SIZE, 0);
    if (send_res < 0)
    {
        PRINT_SOCKET_ERROR("Error sending msg to server");
    }

    free(buff);
}

void handle_room_list(Message* msg)
{
    profile.chat_state = IN_ROOM_MENU;

    // 0 set to create new room
    printf("Room Options:\n");
    printf("0 -> Create new room\n");

    // have active rooms to send
    int count = 1;
    if (msg->header.payload_length > 0)
    {
        profile.num_active_rooms = msg->payload.room_list.num_active_rooms;
        memcpy(profile.rooms, msg->payload.room_list.rooms, msg->payload.room_list.num_active_rooms * sizeof(RoomInfo));

        for (int i = 0; i < msg->payload.room_list.num_active_rooms; i++)
        {
            RoomInfo curr_room = msg->payload.room_list.rooms[i];
            printf("%i -> Room: \"%s\" (id: %i)\n", count, curr_room.server_name, curr_room.server_id);
            count++;
        }
    }
}

void handle_join_room_response(Message* msg)
{
    if (msg->payload.join_room_res.joined_result == false)
    {
        printf("The server wasn't able to add you to the room you requested, try a different room\n");
        profile.chat_state = IN_ROOM_MENU;
        return;
    }

    // set this client's active room and update status
    profile.activeRoom = msg->payload.join_room_res.joined_room;
    profile.chat_state = IN_ROOM;
    printf("You've been added to the room: %s! Start sending messages\n", msg->payload.join_room_res.joined_room.server_name);
}

void handle_broadcast_msg(Message* msg)
{
    // print message sender and message
    printf("%s> %s\n", msg->payload.new_msg.username, msg->payload.new_msg.msg);
}

void route_server_message(Message* msg)
{
    switch (msg->header.message_type)
    {
    case S2C_ROOM_LIST:
        handle_room_list(msg);
        break;
    case S2C_JOIN_ROOM_RES:
        handle_join_room_response(msg);
        break;
    case S2C_BROADCAST_MSG:
        handle_broadcast_msg(msg);
        break;
    default:
        printf("Unrecognized message type: %d\n", msg->header.message_type);
    }
}

void send_join_room(SOCKET_T socketfd, int input)
{
    // create C2S_JOIN_ROOM message
    Message msg = {0};
    msg.header.message_type = C2S_JOIN_ROOM;
    msg.header.client_id = profile.id;
    msg.header.version = PROTOCOL_VERSION;
    msg.header.sequence_number = ++profile.sequence_num;
    msg.header.payload_length = ROOM_INFO_SIZE;

    RoomInfo room_to_join = profile.rooms[input - 1];
    msg.payload.join_room.room_to_join = room_to_join;

    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);
    int send_res = send(socketfd, (const char*)buff, HEADER_SIZE + ROOM_INFO_SIZE, 0);

    if (send_res < 0)
    {
        PRINT_SOCKET_ERROR("Error sending msg to server");
    }

    free(buff);
}

void send_create_room(SOCKET_T socketfd, char* input)
{
    Message msg = {0};
    msg.header.message_type = C2S_CREATE_ROOM;
    msg.header.client_id = profile.id;
    msg.header.version = PROTOCOL_VERSION;
    msg.header.sequence_number = ++profile.sequence_num;
    msg.header.payload_length = 21;

    memcpy(msg.payload.create_room.server_name, input, strlen(input) + 1);
    msg.payload.create_room.server_name[20] = '\0';

    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);
    int send_res = send(socketfd, (const char*)buff, HEADER_SIZE + 21, 0);

    if (send_res < 0)
    {
        PRINT_SOCKET_ERROR("Error sending msg to server");
    }

    free(buff);
}

void send_new_chat(SOCKET_T socketfd, char* input)
{
    // package message
    Message msg = {0};
    msg.header.client_id = profile.id;
    msg.header.message_type = C2S_NEW_MSG;
    msg.header.sequence_number = profile.sequence_num++;
    msg.header.payload_length = ROOM_INFO_SIZE + strlen(input) + 21;
    msg.header.version = PROTOCOL_VERSION;

    msg.payload.new_msg.target_room = profile.activeRoom;
    memcpy(msg.payload.new_msg.username, profile.username, 21);
    memcpy(msg.payload.new_msg.msg, input, strlen(input));

    // serialize and send
    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);
    int send_res = send(socketfd, (const char*)buff, HEADER_SIZE + msg.header.payload_length, 0);
    if (send_res < 0)
    {
        PRINT_SOCKET_ERROR("Error sending message to the server");
    }

    free(buff);
}

static void send_leave(SOCKET_T socketfd)
{
    Message msg = {0};
    msg.header.message_type = C2S_LEAVE;
    msg.header.client_id = profile.id;
    msg.header.payload_length = 0;
    msg.header.sequence_number = ++profile.sequence_num;
    msg.header.version = PROTOCOL_VERSION;

    uint8_t* buff = calloc(1, sizeof(Message));
    serialize(&msg, buff);
    int send_res = send(socketfd, (const char*)buff, HEADER_SIZE, 0);

    if (send_res < 0)
    {
        PRINT_SOCKET_ERROR("Error sending message to the server");
    }

    free(buff);
}

static void handle_input_line(SOCKET_T socketfd, char* input)
{
    input[strcspn(input, "\n")] = 0;

    // exit check
    if (strcmp(input, "/quit") == 0)
    {
        send_leave(socketfd);
        CLOSE_SOCKET(socketfd);
#ifdef _WIN32
        cleanup_stdin_queue();
        WSACleanup();
#endif
        printf("Goodbye!\n");
        exit(EXIT_SUCCESS);
    }

    switch (profile.chat_state)
    {
    case AWAITING_ROOM_LIST:
        // shouldn't be possible
        break;
    case IN_ROOM_MENU:;
        char* end;
        long input_num = strtol(input, &end, 10);

        // validate input
        if (end != input && *end == '\0' &&
            input_num >= 0 && input_num <= profile.num_active_rooms)
        {
            // making a new room
            if (input_num == 0)
            {
                profile.chat_state = CREATING_ROOM;
                printf("Enter a room name: \n");
            }
            // joining existing room
            else
            {
                // send join request for selected room
                send_join_room(socketfd, input_num);
            }
        }
        else
        {
            printf("Selection must be >= 0 and <= %i, try again!\n", profile.num_active_rooms);
        }
        break;
    case CREATING_ROOM:
        // chop input at 20 characters
        if (strlen(input) >= MAX_SERVERNAME_LEN)
        {
            input[MAX_SERVERNAME_LEN - 1] = '\0';
        }

        send_create_room(socketfd, input);
        break;
    case IN_ROOM:
        send_new_chat(socketfd, input);
        break;
    default:
        printf("Unrecognized chat state, resetting client...\n");
        break;
    }
}

static void process_poll_events(struct pollfd* pfds, SOCKET_T socketfd)
{
#ifdef _WIN32
    // Check if background stdin thread has read any input line
    char input_line[MAX_CHAT_MSG_SIZE];
    if (try_pop_stdin(input_line, sizeof(input_line)))
    {
        handle_input_line(socketfd, input_line);
    }
#else
    // POSIX user inputted new msg
    if ((pfds[1].fd == STDIN_FILENO) && (pfds[1].revents & POLLIN))
    {
        char input[MAX_CHAT_MSG_SIZE];
        fgets(input, MAX_CHAT_MSG_SIZE, stdin);
        handle_input_line(socketfd, input);
    }
#endif

    // new msg from server
    if ((pfds[0].fd == socketfd) && (pfds[0].revents & POLLIN))
    {
        Message msg = {0};
        int result = recv_message(pfds[0].fd, &profile.state, &msg);

        if (result == -1)
        {
            perror("Server disconnected or internal msg error\n");
#ifdef _WIN32
            cleanup_stdin_queue();
            WSACleanup();
#endif
            exit(EXIT_FAILURE);
        }
        else if (result == 1)
        {
            // set client_id if not done yet
            if (profile.id == 0)
            {
                profile.id = msg.header.client_id;
            }

            route_server_message(&msg);
        }
    }

    // server hang up or error
    if ((pfds[0].fd == socketfd) && ((pfds[0].revents & POLLHUP) || (pfds[0].revents & POLLERR)))
    {
        printf("Server closed due to error\n");
#ifdef _WIN32
        cleanup_stdin_queue();
        WSACleanup();
#endif
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[])
{
    const char* server_ip = parse_args(argc, argv);
    SOCKET_T socketfd = INVALID_SOCKET_T;

    setup_user(stdin);
    socket_init(&socketfd, server_ip);

#ifdef _WIN32
    init_stdin_queue();
#endif

    send_new_join(socketfd);

    struct pollfd pfds[2];
    poll_init(pfds, socketfd);

    while (1)
    {
#ifdef _WIN32
        int n = poll(pfds, 1, 100); // On Windows poll only server socket, with 100ms timeout
#else
        int n = poll(pfds, 2, -1); // On POSIX poll server socket and stdin, block forever
#endif

        if (n < 0)
        {
            PRINT_SOCKET_ERROR("Error polling for new events");
            continue;
        }

        process_poll_events(pfds, socketfd);
    }

    // close client socket when done
    CLOSE_SOCKET(socketfd);
    printf("Socket closed\n");

#ifdef _WIN32
    cleanup_stdin_queue();
    WSACleanup();
#endif

    return 0;
}
