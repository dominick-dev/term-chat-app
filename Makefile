# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -I$(INCLUDE_DIR)

# Directories
SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include

# Headers
PROTOCOL_H = $(INCLUDE_DIR)/protocol.h
LOGGER_H = $(INCLUDE_DIR)/logger.h

# Shared src files
SHARED_LOGGER = $(SRC_DIR)/shared/logger.c
SHARED_PROTOCOL = $(SRC_DIR)/shared/protocol.c

# Client and server src files
CLIENT_SRC = $(SRC_DIR)/client/client.c # add ui.c later?
SERVER_SRC = $(SRC_DIR)/server/server.c

# Shared obj files
LOGGER_OBJ = $(patsubst $(SRC_DIR)/shared/%.c,$(BUILD_DIR)/%.o,$(SHARED_LOGGER))
PROTOCOL_OBJ= $(patsubst $(SRC_DIR)/shared/%.c,$(BUILD_DIR)/%.o,$(SHARED_PROTOCOL))
CLIENT_OBJ = $(patsubst $(SRC_DIR)/client/%.c,$(BUILD_DIR)/%.o,$(CLIENT_SRC))
SERVER_OBJ = $(patsubst $(SRC_DIR)/server/%.c,$(BUILD_DIR)/%.o,$(SERVER_SRC))

# Executables
CLIENT = $(BUILD_DIR)/client
SERVER = $(BUILD_DIR)/server

# Default
all: $(BUILD_DIR) $(CLIENT) $(SERVER)

# Create build dir structure
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build client
$(CLIENT): $(CLIENT_OBJ) $(LOGGER_OBJ) $(PROTOCOL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lncurses

# Build Server
$(SERVER): $(SERVER_OBJ) $(LOGGER_OBJ) $(PROTOCOL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile obj files from shared/
$(BUILD_DIR)/%.o: $(SRC_DIR)/shared/%.c $(PROTOCOL_H) $(LOGGER_H)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile obj files from client/
$(BUILD_DIR)/%.o: $(SRC_DIR)/client/%.c $(PROTOCOL_H) $(LOGGER_H)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile obj files from server/
$(BUILD_DIR)/%.o: $(SRC_DIR)/server/%.c $(PROTOCOL_H) $(LOGGER_H)
	$(CC) $(CFLAGS) -c $< -o $@

# Run targets
client-run: $(CLIENT)
	./$(CLIENT) $(IP)

server-run: $(SERVER)
	./$(SERVER)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean run-client run-server
