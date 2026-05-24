#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    #define SOCKET_T            SOCKET
    #define INVALID_SOCKET_T    INVALID_SOCKET
    #define CLOSE_SOCKET(s)     closesocket(s)
    #define SOCKET_ERROR_T      SOCKET_ERROR

    #define poll                WSAPoll

    #define STDIN_FILENO        0

    typedef int ssize_t;

    #define PRINT_SOCKET_ERROR(msg) \
        fprintf(stderr, msg " failed with error: %d\n", WSAGetLastError())
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/poll.h>
    #include <unistd.h>
    #include <errno.h>

    #define SOCKET_T            int
    #define INVALID_SOCKET_T    (-1)
    #define CLOSE_SOCKET(s)     close(s)
    #define SOCKET_ERROR_T      (-1)

    #define PRINT_SOCKET_ERROR(msg) perror(msg)
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#endif
