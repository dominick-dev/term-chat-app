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
    GETTING_USERNAME,
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
#define MAX_INPUT_LEN 512

typedef struct
{
    char lines[STDIN_QUEUE_SIZE][MAX_INPUT_LEN];
    int head;
    int tail;
    int count;
    CRITICAL_SECTION lock;
    HANDLE data_ready; // manual-reset event
    HANDLE hThread;
} StdinQueue;

static StdinQueue g_stdin_queue;

static unsigned __stdcall stdin_reader_thread(void* arg)
{
    (void)arg;
    char buf[MAX_INPUT_LEN];
    while (fgets(buf, sizeof(buf), stdin) != NULL)
    {
        EnterCriticalSection(&g_stdin_queue.lock);
        if (g_stdin_queue.count < STDIN_QUEUE_SIZE)
        {
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

void init_stdin_queue(void)
{
    memset(&g_stdin_queue, 0, sizeof(g_stdin_queue));
    InitializeCriticalSection(&g_stdin_queue.lock);
    g_stdin_queue.data_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_stdin_queue.hThread = (HANDLE)_beginthreadex(NULL, 0, stdin_reader_thread, NULL, 0, NULL);
}

int try_pop_stdin(char* out, int max_len)
{
    int got = 0;
    EnterCriticalSection(&g_stdin_queue.lock);
    if (g_stdin_queue.count > 0)
    {
        strncpy(out, g_stdin_queue.lines[g_stdin_queue.head], max_len - 1);
        out[max_len - 1] = '\0';
        g_stdin_queue.head = (g_stdin_queue.head + 1) % STDIN_QUEUE_SIZE;
        g_stdin_queue.count--;
        if (g_stdin_queue.count == 0)
            ResetEvent(g_stdin_queue.data_ready);
        got = 1;
    }
    LeaveCriticalSection(&g_stdin_queue.lock);
    return got;
}

void cleanup_stdin_queue(void)
{
    if (g_stdin_queue.hThread)
    {
        CloseHandle(g_stdin_queue.hThread);
        g_stdin_queue.hThread = NULL;
    }
    CloseHandle(g_stdin_queue.data_ready);
    DeleteCriticalSection(&g_stdin_queue.lock);
}
#else
// POSIX ncurses dependencies
#include <locale.h>
#include <ncurses.h>

typedef struct
{
    char username[21];
    char msg[MAX_CHAT_MSG_SIZE];
    bool is_system;
} ChatMessage;

#define MAX_HISTORY 200
static ChatMessage chat_history[MAX_HISTORY];
static int history_count = 0;
static int scroll_offset = 0;

static WINDOW* header_win = NULL;
static WINDOW* main_win = NULL;
static WINDOW* input_win = NULL;

static char input_buffer[MAX_CHAT_MSG_SIZE] = {0};
static SOCKET_T g_socketfd = INVALID_SOCKET_T;
static const char* g_server_ip = NULL;

static void add_to_history(const char* username, const char* msg, bool is_system)
{
    if (history_count < MAX_HISTORY)
    {
        strncpy(chat_history[history_count].username, username, 20);
        chat_history[history_count].username[20] = '\0';
        strncpy(chat_history[history_count].msg, msg, MAX_CHAT_MSG_SIZE - 1);
        chat_history[history_count].msg[MAX_CHAT_MSG_SIZE - 1] = '\0';
        chat_history[history_count].is_system = is_system;
        history_count++;
    }
    else
    {
        // Shift left
        for (int i = 1; i < MAX_HISTORY; i++)
        {
            chat_history[i - 1] = chat_history[i];
        }
        strncpy(chat_history[MAX_HISTORY - 1].username, username, 20);
        chat_history[MAX_HISTORY - 1].username[20] = '\0';
        strncpy(chat_history[MAX_HISTORY - 1].msg, msg, MAX_CHAT_MSG_SIZE - 1);
        chat_history[MAX_HISTORY - 1].msg[MAX_CHAT_MSG_SIZE - 1] = '\0';
        chat_history[MAX_HISTORY - 1].is_system = is_system;
    }
}

static void cleanup_ncurses(void)
{
    if (!isendwin())
    {
        endwin();
    }
}

static void init_ui_windows(void)
{
    if (header_win)
        delwin(header_win);
    if (main_win)
        delwin(main_win);
    if (input_win)
        delwin(input_win);

    header_win = newwin(3, COLS, 0, 0);
    main_win = newwin(LINES - 6, COLS, 3, 0);
    input_win = newwin(3, COLS, LINES - 3, 0);

    keypad(input_win, TRUE);
    nodelay(input_win, TRUE);
}

static int wrap_message(const char* msg, int max_width, char lines_out[][MAX_CHAT_MSG_SIZE], int max_lines)
{
    int len = strlen(msg);
    if (len == 0)
    {
        strncpy(lines_out[0], "", MAX_CHAT_MSG_SIZE);
        return 1;
    }
    int num_lines = 0;
    int src_idx = 0;
    while (src_idx < len && num_lines < max_lines)
    {
        int chunk_size = len - src_idx;
        if (chunk_size > max_width)
        {
            chunk_size = max_width;

            // Try to find space to wrap at a word boundary
            int space_idx = -1;
            for (int i = chunk_size - 1; i >= chunk_size / 2; i--)
            {
                if (msg[src_idx + i] == ' ')
                {
                    space_idx = i;
                    break;
                }
            }
            if (space_idx != -1)
            {
                chunk_size = space_idx;
            }
        }

        strncpy(lines_out[num_lines], msg + src_idx, chunk_size);
        lines_out[num_lines][chunk_size] = '\0';
        num_lines++;

        src_idx += chunk_size;
        // Skip leading space if wrapped at a space
        if (src_idx < len && msg[src_idx] == ' ')
        {
            src_idx++;
        }
    }
    return num_lines;
}

static void draw_ui(void)
{
    if (isendwin())
        return;

    // --- Header Window ---
    werase(header_win);
    box(header_win, 0, 0);

    wattron(header_win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(header_win, 1, (COLS - 18) / 2, " TERMINAL CHAT ");
    wattroff(header_win, COLOR_PAIR(1) | A_BOLD);

    if (profile.chat_state != GETTING_USERNAME)
    {
        mvwprintw(header_win, 1, 2, "User: %s", profile.username);
    }

    const char* state_str = "Login";
    if (profile.chat_state == AWAITING_ROOM_LIST)
        state_str = "Connecting...";
    else if (profile.chat_state == IN_ROOM_MENU)
        state_str = "Selecting Room";
    else if (profile.chat_state == CREATING_ROOM)
        state_str = "Creating Room";
    else if (profile.chat_state == IN_ROOM)
        state_str = (char*)profile.activeRoom.server_name;

    mvwprintw(header_win, 1, COLS - 25, "Room/State: %-12s", state_str);
    wrefresh(header_win);

    // --- Main Window ---
    werase(main_win);
    box(main_win, 0, 0);

    int max_y = (LINES - 6) - 2;
    int max_x = COLS - 2;

    if (profile.chat_state == GETTING_USERNAME)
    {
        wattron(main_win, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(main_win, max_y / 2 - 3, (COLS - 28) / 2, "Welcome to Terminal Chat!");
        wattroff(main_win, COLOR_PAIR(1) | A_BOLD);

        mvwprintw(main_win, max_y / 2 - 1, (COLS - 36) / 2, "Please enter your username below.");
        mvwprintw(main_win, max_y / 2, (COLS - 42) / 2, "Use the input window at the bottom screen.");
        mvwprintw(main_win, max_y / 2 + 1, (COLS - 22) / 2, "(Maximum 20 characters)");
    }
    else if (profile.chat_state == AWAITING_ROOM_LIST)
    {
        mvwprintw(main_win, max_y / 2, (COLS - 28) / 2, "Connecting to server...");
        mvwprintw(main_win, max_y / 2 + 1, (COLS - 30) / 2, "Waiting for initial room list");
    }
    else if (profile.chat_state == IN_ROOM_MENU)
    {
        wattron(main_win, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(main_win, 1, 2, "AVAILABLE CHAT ROOMS:");
        wattroff(main_win, COLOR_PAIR(1) | A_BOLD);

        mvwhline(main_win, 2, 2, ACS_HLINE, COLS - 4);

        wattron(main_win, COLOR_PAIR(3));
        mvwprintw(main_win, 4, 4, "0 -> [Create new room]");
        wattroff(main_win, COLOR_PAIR(3));

        if (profile.num_active_rooms > 0)
        {
            for (int i = 0; i < profile.num_active_rooms && (i + 6) < max_y; i++)
            {
                RoomInfo curr_room = profile.rooms[i];
                mvwprintw(main_win, i + 6, 4, "%d -> Room: \"%s\" (ID: %d)", i + 1, curr_room.server_name, curr_room.server_id);
            }
        }
        else
        {
            mvwprintw(main_win, 6, 4, "No active rooms found. Enter '0' to create one!");
        }
    }
    else if (profile.chat_state == CREATING_ROOM)
    {
        wattron(main_win, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(main_win, 1, 2, "CREATE A NEW CHAT ROOM:");
        wattroff(main_win, COLOR_PAIR(1) | A_BOLD);

        mvwhline(main_win, 2, 2, ACS_HLINE, COLS - 4);

        mvwprintw(main_win, 4, 4, "Type the name for your new room in the input box below.");
        mvwprintw(main_win, 5, 4, "Press Enter when done. (Maximum 20 characters)");
    }
    else if (profile.chat_state == IN_ROOM)
    {
        int out_y = max_y;
        int curr_msg_idx = (history_count - 1) - scroll_offset;

        if (curr_msg_idx < 0)
            curr_msg_idx = 0;
        if (curr_msg_idx >= history_count)
            curr_msg_idx = history_count - 1;

        while (curr_msg_idx >= 0 && out_y >= 1)
        {
            ChatMessage chat = chat_history[curr_msg_idx];

            if (chat.is_system)
            {
                int max_width = max_x - 8;
                if (max_width < 10)
                    max_width = 10;

                char wrapped_lines[10][MAX_CHAT_MSG_SIZE];
                int L = wrap_message(chat.msg, max_width, wrapped_lines, 10);

                for (int line_idx = L - 1; line_idx >= 0; line_idx--)
                {
                    if (out_y >= 1)
                    {
                        wattron(main_win, COLOR_PAIR(3));
                        mvwprintw(main_win, out_y, 2, "*** %s ***", wrapped_lines[line_idx]);
                        wattroff(main_win, COLOR_PAIR(3));
                        out_y--;
                    }
                }
            }
            else
            {
                bool is_me = (strcmp(chat.username, profile.username) == 0);
                int prefix_len = strlen(chat.username) + (is_me ? 9 : 2);
                int max_width = max_x - prefix_len - 2;
                if (max_width < 10)
                    max_width = 10;

                char wrapped_lines[20][MAX_CHAT_MSG_SIZE];
                int L = wrap_message(chat.msg, max_width, wrapped_lines, 20);

                for (int line_idx = L - 1; line_idx >= 0; line_idx--)
                {
                    if (out_y >= 1)
                    {
                        if (line_idx == 0)
                        {
                            if (is_me)
                            {
                                wattron(main_win, COLOR_PAIR(1) | A_BOLD);
                                mvwprintw(main_win, out_y, 2, "%s (You)> ", chat.username);
                                wattroff(main_win, COLOR_PAIR(1) | A_BOLD);
                            }
                            else
                            {
                                wattron(main_win, COLOR_PAIR(2) | A_BOLD);
                                mvwprintw(main_win, out_y, 2, "%s> ", chat.username);
                                wattroff(main_win, COLOR_PAIR(2) | A_BOLD);
                            }
                            mvwprintw(main_win, out_y, 2 + prefix_len, "%s", wrapped_lines[line_idx]);
                        }
                        else
                        {
                            mvwprintw(main_win, out_y, 2 + prefix_len, "%s", wrapped_lines[line_idx]);
                        }
                        out_y--;
                    }
                }
            }
            curr_msg_idx--;
        }

        // Draw scroll indicators on borders if applicable
        if (history_count > 0)
        {
            // If there is older history off the top
            int top_unseen_idx = (history_count - 1) - scroll_offset - (max_y - out_y);
            if (top_unseen_idx >= 0)
            {
                wattron(main_win, COLOR_PAIR(1) | A_BOLD);
                mvwaddch(main_win, 0, COLS - 3, ACS_UARROW);
                wattroff(main_win, COLOR_PAIR(1) | A_BOLD);
            }
            // If we are scrolled up and there is newer history off the bottom
            if (scroll_offset > 0)
            {
                wattron(main_win, COLOR_PAIR(1) | A_BOLD);
                mvwaddch(main_win, (LINES - 6) - 1, COLS - 3, ACS_DARROW);
                wattroff(main_win, COLOR_PAIR(1) | A_BOLD);
            }
        }
    }
    wrefresh(main_win);

    // --- Input Window ---
    werase(input_win);
    box(input_win, 0, 0);

    const char* prompt = "Send: ";
    if (profile.chat_state == GETTING_USERNAME)
        prompt = "Username: ";
    else if (profile.chat_state == IN_ROOM_MENU)
        prompt = "Select Option: ";
    else if (profile.chat_state == CREATING_ROOM)
        prompt = "Room Name: ";

    wattron(input_win, COLOR_PAIR(5) | A_BOLD);
    mvwprintw(input_win, 1, 2, "%s", prompt);
    wattroff(input_win, COLOR_PAIR(5) | A_BOLD);

    int prompt_len = strlen(prompt);
    int input_max_width = COLS - prompt_len - 6;
    int input_len = strlen(input_buffer);
    const char* visible_input = input_buffer;
    if (input_len > input_max_width)
    {
        visible_input = input_buffer + (input_len - input_max_width);
    }

    mvwprintw(input_win, 1, 2 + prompt_len, "%s", visible_input);

    int cursor_x = 2 + prompt_len + strlen(visible_input);
    wmove(input_win, 1, cursor_x);
    wrefresh(input_win);
}
#endif

static const char* parse_args(int num_args, char** args_arr)
{
    if (num_args == 1)
    {
#ifdef _WIN32
        printf("Connecting to local server\n");
#endif
        return "127.0.0.1";
    }
    else if (num_args == 2)
    {
#ifdef _WIN32
        printf("Connecting to pi server\n");
#endif
        return args_arr[1];
    }
    else
    {
#ifdef _WIN32
        printf("Usage: %s <server_ip>\n", args_arr[0]);
#else
        fprintf(stderr, "Usage: %s <server_ip>\n", args_arr[0]);
#endif
        exit(EXIT_FAILURE);
    }
}

#ifdef _WIN32
static void poll_init(struct pollfd* pfds, SOCKET_T socketfd)
{
    profile.state.ActiveState = WAITING;

    // add server to pfds
    pfds[0].fd = socketfd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
}
#endif

/*
 * Initializes the client socket
 */
static void socket_init(SOCKET_T* socketfd, const char* server_ip)
{
    LOG_INFO(__FUNCTION__, "initializing the client...\n");
    profile.state.ActiveState = WAITING;

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        fprintf(stderr, "WSAStartup failed\n");
        exit(EXIT_FAILURE);
    }
#endif

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0)
    {
#ifdef _WIN32
        perror("Invalid server IP address");
        WSACleanup();
#else
        cleanup_ncurses();
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
#endif
        exit(EXIT_FAILURE);
    }
    serv_addr.sin_port = htons(PORT);

    // create client socket
    *socketfd = socket(PF_INET, SOCK_STREAM, 0);
    if (*socketfd == INVALID_SOCKET_T)
    {
#ifdef _WIN32
        PRINT_SOCKET_ERROR("Error creating client socket");
        WSACleanup();
#else
        cleanup_ncurses();
        perror("Error creating client socket");
#endif
        exit(EXIT_FAILURE);
    }

    // attempt to connect to server socket
    int conn_res = connect(*socketfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (conn_res != 0)
    {
#ifdef _WIN32
        PRINT_SOCKET_ERROR("Error connecting to server socket");
        CLOSE_SOCKET(*socketfd);
        WSACleanup();
#else
        cleanup_ncurses();
        perror("Error connecting to server socket");
        CLOSE_SOCKET(*socketfd);
#endif
        exit(EXIT_FAILURE);
    }

    LOG_INFO(__FUNCTION__, "Connected to server");
#ifdef _WIN32
    printf("Connected to server!\n");
#endif
}

#ifdef _WIN32
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
#endif

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

#ifdef _WIN32
    // 0 set to create new room
    printf("Room Options:\n");
    printf("0 -> Create new room\n");
#endif

    if (msg->header.payload_length > 0)
    {
        profile.num_active_rooms = msg->payload.room_list.num_active_rooms;
        memcpy(profile.rooms, msg->payload.room_list.rooms, msg->payload.room_list.num_active_rooms * sizeof(RoomInfo));

#ifdef _WIN32
        int count = 1;
        for (int i = 0; i < msg->payload.room_list.num_active_rooms; i++)
        {
            RoomInfo curr_room = msg->payload.room_list.rooms[i];
            printf("%i -> Room: \"%s\" (id: %i)\n", count, curr_room.server_name, curr_room.server_id);
            count++;
        }
#endif
    }
    else
    {
        profile.num_active_rooms = 0;
    }
}

void handle_join_room_response(Message* msg)
{
    if (msg->payload.join_room_res.joined_result == false)
    {
#ifdef _WIN32
        printf("The server wasn't able to add you to the room you requested, try a different room\n");
#else
        add_to_history("System", "The server rejected your request to join that room. Please select another.", true);
#endif
        profile.chat_state = IN_ROOM_MENU;
        return;
    }

    // set this client's active room and update status
    profile.activeRoom = msg->payload.join_room_res.joined_room;
    profile.chat_state = IN_ROOM;

#ifdef _WIN32
    printf("You've been added to the room: %s! Start sending messages\n", msg->payload.join_room_res.joined_room.server_name);
#else
    history_count = 0; // Clear chat history for the new room
    char welcome[128];
    snprintf(welcome, sizeof(welcome), "Joined room: %s! Start sending messages", msg->payload.join_room_res.joined_room.server_name);
    add_to_history("System", welcome, true);
#endif
}

void handle_broadcast_msg(Message* msg)
{
#ifdef _WIN32
    // print message sender and message
    printf("%s> %s\n", msg->payload.new_msg.username, msg->payload.new_msg.msg);
#else
    add_to_history((char*)msg->payload.new_msg.username, msg->payload.new_msg.msg, false);
    scroll_offset = 0;
#endif
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
#ifdef _WIN32
        printf("Unrecognized message type: %d\n", msg->header.message_type);
#else
    {
        char errmsg[64];
        snprintf(errmsg, sizeof(errmsg), "Unrecognized message type: %d", msg->header.message_type);
        add_to_history("System", errmsg, true);
    }
#endif
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
#ifndef _WIN32
    else
    {
        add_to_history(profile.username, input, false);
        scroll_offset = 0;
    }
#endif

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
#ifndef _WIN32
        cleanup_ncurses();
#endif
        if (socketfd != INVALID_SOCKET_T)
        {
            send_leave(socketfd);
            CLOSE_SOCKET(socketfd);
        }
#ifdef _WIN32
        cleanup_stdin_queue();
        WSACleanup();
#endif
        printf("Goodbye!\n");
        exit(EXIT_SUCCESS);
    }

    switch (profile.chat_state)
    {
#ifndef _WIN32
    case GETTING_USERNAME:
        if (strlen(input) == 0)
        {
            break;
        }
        strncpy(profile.username, input, MAX_USERNAME_LEN - 1);
        profile.username[MAX_USERNAME_LEN - 1] = '\0';
        profile.sequence_num = 0;
        profile.chat_state = AWAITING_ROOM_LIST;

        socket_init(&g_socketfd, g_server_ip);
        send_new_join(g_socketfd);
        break;
#endif
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
#ifdef _WIN32
                printf("Enter a room name: \n");
#endif
            }
            // joining existing room
            else
            {
                // send join request for selected room
                send_join_room(socketfd, input_num);
#ifndef _WIN32
                profile.chat_state = AWAITING_ROOM_LIST; // wait for response
#endif
            }
        }
        else
        {
#ifdef _WIN32
            printf("Selection must be >= 0 and <= %i, try again!\n", profile.num_active_rooms);
#else
            add_to_history("System", "Invalid choice. Select 0 to create, or a valid room list index.", true);
#endif
        }
        break;
    case CREATING_ROOM:
        // chop input at 20 characters
        if (strlen(input) >= MAX_SERVERNAME_LEN)
        {
            input[MAX_SERVERNAME_LEN - 1] = '\0';
        }

        send_create_room(socketfd, input);
#ifndef _WIN32
        profile.chat_state = AWAITING_ROOM_LIST;
#endif
        break;
    case IN_ROOM:
        send_new_chat(socketfd, input);
        break;
    default:
#ifdef _WIN32
        printf("Unrecognized chat state, resetting client...\n");
#else
        add_to_history("System", "Unrecognized chat state, resetting...", true);
#endif
        break;
    }
}

#ifdef _WIN32
static void process_poll_events(struct pollfd* pfds, SOCKET_T socketfd)
{
    // Check if background stdin thread has read any input line
    char input_line[MAX_CHAT_MSG_SIZE];
    if (try_pop_stdin(input_line, sizeof(input_line)))
    {
        handle_input_line(socketfd, input_line);
    }

    // new msg from server
    if ((pfds[0].fd == socketfd) && (pfds[0].revents & POLLIN))
    {
        Message msg = {0};
        int result = recv_message(pfds[0].fd, &profile.state, &msg);

        if (result == -1)
        {
            perror("Server disconnected or internal msg error\n");
            cleanup_stdin_queue();
            WSACleanup();
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
        cleanup_stdin_queue();
        WSACleanup();
        exit(EXIT_FAILURE);
    }
}
#endif

int main(int argc, char* argv[])
{
    const char* server_ip = parse_args(argc, argv);

#ifdef _WIN32
    SOCKET_T socketfd = INVALID_SOCKET_T;

    setup_user(stdin);
    socket_init(&socketfd, server_ip);

    init_stdin_queue();

    send_new_join(socketfd);

    struct pollfd pfds[2];
    poll_init(pfds, socketfd);

    while (1)
    {
        int n = poll(pfds, 1, 100); // On Windows poll only server socket, with 100ms timeout

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

    cleanup_stdin_queue();
    WSACleanup();

    return 0;
#else
    // POSIX ncurses initialization
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    atexit(cleanup_ncurses);

    if (has_colors())
    {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_RED, COLOR_BLACK);
        init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
    }

    init_ui_windows();

    g_server_ip = server_ip;
    profile.chat_state = GETTING_USERNAME;
    memset(input_buffer, 0, sizeof(input_buffer));
    draw_ui();

    struct pollfd pfds[2];

    while (1)
    {
        int p_idx = 0;
        if (g_socketfd != INVALID_SOCKET_T)
        {
            pfds[p_idx].fd = g_socketfd;
            pfds[p_idx].events = POLLIN;
            pfds[p_idx].revents = 0;
            p_idx++;
        }

        pfds[p_idx].fd = STDIN_FILENO;
        pfds[p_idx].events = POLLIN;
        pfds[p_idx].revents = 0;
        p_idx++;

        int n = poll(pfds, p_idx, -1);
        if (n < 0)
        {
            init_ui_windows();
            draw_ui();
            continue;
        }

        int stdin_ready = 0;
        int socket_ready = 0;
        int socket_hung_up = 0;

        for (int i = 0; i < p_idx; i++)
        {
            if (pfds[i].fd == STDIN_FILENO && (pfds[i].revents & POLLIN))
            {
                stdin_ready = 1;
            }
            if (g_socketfd != INVALID_SOCKET_T && pfds[i].fd == g_socketfd)
            {
                if (pfds[i].revents & POLLIN)
                {
                    socket_ready = 1;
                }
                if (pfds[i].revents & (POLLHUP | POLLERR))
                {
                    socket_hung_up = 1;
                }
            }
        }

        if (stdin_ready)
        {
            int ch;
            while ((ch = wgetch(input_win)) != ERR)
            {
                if (ch == KEY_RESIZE)
                {
                    init_ui_windows();
                    draw_ui();
                }
                else if (ch == KEY_UP || ch == KEY_PPAGE)
                {
                    if (profile.chat_state == IN_ROOM)
                    {
                        int amount = (ch == KEY_PPAGE) ? 5 : 1;
                        if (scroll_offset + amount < history_count)
                        {
                            scroll_offset += amount;
                        }
                        else
                        {
                            scroll_offset = history_count - 1;
                            if (scroll_offset < 0)
                                scroll_offset = 0;
                        }
                        draw_ui();
                    }
                }
                else if (ch == KEY_DOWN || ch == KEY_NPAGE)
                {
                    if (profile.chat_state == IN_ROOM)
                    {
                        int amount = (ch == KEY_NPAGE) ? 5 : 1;
                        if (scroll_offset - amount >= 0)
                        {
                            scroll_offset -= amount;
                        }
                        else
                        {
                            scroll_offset = 0;
                        }
                        draw_ui();
                    }
                }
                else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
                {
                    handle_input_line(g_socketfd, input_buffer);
                    memset(input_buffer, 0, sizeof(input_buffer));
                    draw_ui();
                }
                else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == '\b')
                {
                    int len = strlen(input_buffer);
                    if (len > 0)
                    {
                        input_buffer[len - 1] = '\0';
                    }
                    draw_ui();
                }
                else if (ch >= 32 && ch <= 126)
                {
                    int len = strlen(input_buffer);
                    if (len < MAX_CHAT_MSG_SIZE - 1)
                    {
                        input_buffer[len] = ch;
                        input_buffer[len + 1] = '\0';
                    }
                    draw_ui();
                }
            }
        }

        if (socket_ready && g_socketfd != INVALID_SOCKET_T)
        {
            Message msg = {0};
            int result = recv_message(g_socketfd, &profile.state, &msg);

            if (result == -1)
            {
                add_to_history("System", "Disconnected from server.", true);
                draw_ui();
                CLOSE_SOCKET(g_socketfd);
                g_socketfd = INVALID_SOCKET_T;
            }
            else if (result == 1)
            {
                if (profile.id == 0)
                {
                    profile.id = msg.header.client_id;
                }
                route_server_message(&msg);
                draw_ui();
            }
        }

        if (socket_hung_up && g_socketfd != INVALID_SOCKET_T)
        {
            add_to_history("System", "Server closed connection.", true);
            draw_ui();
            CLOSE_SOCKET(g_socketfd);
            g_socketfd = INVALID_SOCKET_T;
        }
    }

    cleanup_ncurses();
    return 0;
#endif
}
