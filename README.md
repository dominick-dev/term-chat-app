# term-chat-app

A terminal-based multi-room chat application written in C. Clients connect to a central server, browse or create chat rooms, and exchange messages in real time.

## How it works

The application uses a client-server model with a custom binary protocol over TCP. The server manages all connected clients and rooms. The client provides a minimal terminal UI driven by a simple state machine.

- **Server**: single-threaded event loop using `poll()`, supports up to 100 clients and 100 rooms
- **Client**: polls both the socket and stdin, navigates through room listing, creation, and joining
- **Protocol**: fixed 12-byte header + variable payload, network byte order throughout

## Getting started

**Requirements**: GCC, GNU Make, and a POSIX-compatible OS (Linux or Mac).

```bash
# Build both binaries
make

# In one terminal, start the server
./build/server

# In another terminal, start the client
./build/client              # connects to localhost
./build/client <server_ip>  # connects to a remote server
```

The server listens on port **8080**. When the client starts, you will be prompted for a username, then shown a list of available rooms to join or create.

**Other make targets:**
```bash
make clean       # remove build artifacts
make server-run  # build and run the server
make client-run  # build and run the client
```

## Known issues and in-progress work

- Sequence numbers in broadcast messages are hardcoded to `0` instead of incrementing
- Error cases (room full, room not found, failed creation) print to stdout on the server rather than sending an error response to the client
- Client has no state reset logic on error recovery
- Message serialization sizes use magic numbers instead of named constants
- Some message sending code is duplicated between client and server (should be extracted to shared helpers)
- No input validation on usernames or room names

## Future improvements

- **Windows support** — currently blocked by Winsock vs POSIX sockets, `poll()` incompatibility with console input on Windows, and `localtime_r()` unavailability. A detailed plan is in `WINDOWS_COMPAT_PLAN.md`. The proposed solution is a `platform.h` abstraction layer and a switch to CMake.
- **Configurable settings** — port, max clients, and max rooms are all compile-time constants; exposing these via CLI flags or a config file would be useful
- **Persistent state** — rooms and history are lost when the server restarts
- **Connection timeout handling** — stale clients are not currently detected or cleaned up
- **Private messaging** — no direct messaging between users
- **Authentication** — no password protection for rooms or user accounts
