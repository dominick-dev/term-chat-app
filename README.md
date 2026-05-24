# term-chat-app

A terminal-based multi-room chat application written in C. Clients connect to a central server, browse or create chat rooms, and exchange messages in real time.

## How it works

The application uses a client-server model with a custom binary protocol over TCP. The server manages all connected clients and rooms. The client provides a minimal terminal UI driven by a simple state machine.

- **Server**: single-threaded event loop using `poll()`, supports up to 100 clients and 100 rooms
- **Client**: polls both the socket and stdin, navigates through room listing, creation, and joining
- **Protocol**: fixed 12-byte header + variable payload, network byte order throughout

## Getting started

### Linux / macOS (POSIX)

**Requirements**: GCC, GNU Make.

```bash
# Build both binaries
make

# In one terminal, start the server
./build/server

# In another terminal, start the client
./build/client              # connects to localhost
./build/client <server_ip>  # connects to a remote server
```

**Other make targets:**
```bash
make clean       # remove build artifacts
make server-run  # build and run the server
make client-run  # build and run the client
```

### Windows

**Requirements**: CMake, and a C compiler (e.g., MinGW/GCC or MSVC).

```powershell
# Configure and build both binaries using CMake
cmake -B build -S .
cmake --build build

# Start the client to connect to a server (e.g., localhost or a Raspberry Pi)
.\build\client.exe              # connects to localhost
.\build\client.exe <server_ip>  # connects to a remote server
```

The server listens on port **8080**. When the client starts, you will be prompted for a username, then shown a list of available rooms to join or create.


## Known issues and in-progress work

- Sequence numbers in broadcast messages are hardcoded to `0` instead of incrementing
- Error cases (room full, room not found, failed creation) print to stdout on the server rather than sending an error response to the client
- Client has no state reset logic on error recovery
- Message serialization sizes use magic numbers instead of named constants
- Some message sending code is duplicated between client and server (should be extracted to shared helpers)
- No input validation on usernames or room names

## Future improvements

- **Configurable settings** — port, max clients, and max rooms are all compile-time constants; exposing these via CLI flags or a config file would be useful
- **Persistent state** — rooms and history are lost when the server restarts
- **Connection timeout handling** — stale clients are not currently detected or cleaned up
- **Private messaging** — no direct messaging between users
- **Authentication** — no password protection for rooms or user accounts
