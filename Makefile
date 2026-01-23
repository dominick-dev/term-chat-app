# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -I$(INCLUDE_DIR)

# Directories
SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include

# Shared src files
SHARED_SRC = $(SRC_DIR)/shared/logger.c # add protocol.c when implemented

# Client and server src files
CLIENT_SRC = $(SRC_DIR)/client/client.c # add ui.c later?
SERVER_SRC = $(SRC_DIR)/server/server.c

# Shared obj files
SHARED_OBJ = $(patsubst $(SRC_DIR)/shared/%.c,$(BUILD_DIR)/%.o,$(SHARED_SRC))
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
$(CLIENT): $(CLIENT_OBJ) $(SHARED_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Build Server
$(SERVER): $(SERVER_OBJ) $(SHARED_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile obj files from shared/
$(BUILD_DIR)/%.o: $(SRC_DIR)/shared/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile obj files from client/
$(BUILD_DIR)/%.o: $(SRC_DIR)/client/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile obj files from server/
$(BUILD_DIR)/%.o: $(SRC_DIR)/server/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run targets
client-run: $(CLIENT)
	./$(CLIENT)

server-run: $(SERVER)
	./$(SERVER)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean run-client run-server
