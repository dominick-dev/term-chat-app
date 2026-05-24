cmake_minimum_required(VERSION 3.15)
project(term-chat-app C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Shared library (logger + protocol)
add_library(shared STATIC
    src/shared/logger.c
    src/shared/protocol.c
)
target_include_directories(shared PUBLIC include)

# Client executable
add_executable(client src/client/client.c)
target_link_libraries(client PRIVATE shared)
target_include_directories(client PRIVATE include)

# Server executable
add_executable(server src/server/server.c)
target_link_libraries(server PRIVATE shared)
target_include_directories(server PRIVATE include)

# Windows-specific configuration
if(WIN32)
    # Link Winsock on Windows (required for all socket APIs)
    target_link_libraries(client PRIVATE ws2_32)
    target_link_libraries(server PRIVATE ws2_32)

    # Target Windows Vista or later (required for WSAPoll)
    target_compile_definitions(client PRIVATE _WIN32_WINNT=0x0600)
    target_compile_definitions(server PRIVATE _WIN32_WINNT=0x0600)
    target_compile_definitions(shared PRIVATE _WIN32_WINNT=0x0600)

    # On Windows with MSVC, disable warnings about "unsafe" CRT functions
    target_compile_definitions(client PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(server PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(shared PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Compiler warnings (applied to all platforms)
if(MSVC)
    target_compile_options(client PRIVATE /W4)
    target_compile_options(server PRIVATE /W4)
    target_compile_options(shared PRIVATE /W4)
else()
    target_compile_options(client PRIVATE -Wall -Wextra -g)
    target_compile_options(server PRIVATE -Wall -Wextra -g)
    target_compile_options(shared PRIVATE -Wall -Wextra -g)
endif()
