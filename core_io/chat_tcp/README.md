# Example: TCP Chat System (`chat_tcp`)

This example implements a multi-client, multi-core TCP chat application using the QB C++ Actor Framework, demonstrating a robust architecture for networked applications.

## Overview

The chat system consists of a server and one or more clients that can connect to the server to send and receive messages in a common chat room.

**Server-Side Architecture:**
*   **`AcceptActor`**: Runs on a dedicated core (e.g., Core 0). It listens for incoming TCP connections on one or more ports. Upon accepting a new connection, it distributes the connection to one of the `ServerActor`s in a round-robin fashion.
*   **`ServerActor`** (Pool): Runs on one or more dedicated cores (e.g., Core 1). Each `ServerActor` instance manages multiple client sessions (`ChatSession`). It handles I/O for its clients, relays messages between the clients and the `ChatRoomActor`, and manages session lifecycle.
*   **`ChatSession`**: Not an actor itself, but a class managed by `ServerActor`. Each instance represents a single connected client, handling the `ChatProtocol` for that client's connection, including message parsing and session timeouts.
*   **`ChatRoomActor`**: Runs on a dedicated core (e.g., Core 3). This is the central hub for chat logic. It manages user authentication (ensuring unique usernames), keeps track of all connected sessions, and broadcasts chat messages received from one client to all other connected clients.

**Client-Side Architecture:**
*   **`ClientActor`**: Runs on a dedicated core (e.g., Core 1 on the client machine). It manages the TCP connection to the server, handles sending user messages (received from `InputActor`), and processes incoming messages from the server (authentication responses, chat messages from others, error messages). It also implements reconnection logic.
*   **`InputActor`**: Runs on a dedicated core (e.g., Core 0 on the client machine). It handles user input from the console in a non-blocking way using `qb::ICallback` and sends the input as `ChatInputEvent`s to the `ClientActor`.

**Shared Components (`shared/` directory):**
*   **`Protocol.h/.cpp`**: Defines the custom binary `ChatProtocol` used for communication. This includes message framing (header with magic, version, type, length) and serialization/deserialization logic integrated with QB-IO (`AProtocol`, `pipe::put` specialization).
*   **`Events.h`**: Defines the various `qb::Event` types used for asynchronous communication between the server-side actors (`NewSessionEvent`, `AuthEvent`, `ChatEvent`, `SendMessageEvent`, `DisconnectEvent`) and for client-side input (`ChatInputEvent`).

## QB Features Demonstrated

This example showcases a wide range of QB framework features:

**Core (`qb-core`):**
*   **Actor Model**: `qb::Actor` as the fundamental building block.
*   **Engine Management**: `qb::Main` for initializing and running the actor system.
*   **Multi-Core Deployment**: Strategic placement of actors on different CPU cores using `engine.addActor<T>(core_id, ...)` and `engine.core(core_id).builder()`.
*   **Event System**:
    *   Custom `qb::Event` types for typed, asynchronous communication.
    *   `registerEvent<T>()` and `on(Event&)` for event handling.
*   **Inter-Actor Communication**:
    *   `push<Event>(destination, args...)` for sending messages.
    *   Using `event.getSource()` to identify the sender for replies or context.
    *   `broadcast<qb::KillEvent>()` for system-wide signals (though targeted `KillEvent`s or custom shutdown events are also used).
*   **Actor Lifecycle**: `onInit()`, `kill()`, handling `qb::KillEvent`.
*   **Periodic Callbacks**: `qb::ICallback` and `onCallback()` (used in `InputActor`).
*   **State Management**: Actors like `ChatRoomActor` encapsulate and manage application state.

**I/O (`qb-io`):**
*   **Asynchronous TCP Networking**:
    *   Server-side: `qb::io::use<T>::tcp::acceptor` (`AcceptActor`), `qb::io::use<T>::tcp::server<SessionType>` (`ServerActor` managing `ChatSession`s).
    *   Client-side: `qb::io::use<T>::tcp::client<>` (`ClientActor`).
    *   Connection establishment: `transport().listen_v4()`, `qb::io::async::tcp::connect`.
*   **Session Management**: `ServerActor` using `io_handler` base to manage `ChatSession` instances (`registerSession`, `sessions`).
*   **Custom Protocol Implementation**:
    *   Deriving from `qb::io::async::AProtocol<IO_>` (`ChatProtocol`).
    *   Implementing `getMessageSize()` and `onMessage()` for framing and parsing.
    *   Specializing `qb::allocator::pipe<char>::put<CustomMessage>()` for serialization.
    *   Using `switch_protocol<Protocol>()` to activate the protocol on a connection.
*   **Stream-Based Sending**: Using `*this << message_object << Protocol::end;` for sending data via the protocol.
*   **Timeout Handling**: `qb::io::use<T>::timeout` mixin used by `ChatSession` for inactivity timeouts (`setTimeout`, `updateTimeout`).
*   **URI Parsing**: `qb::io::uri` for handling server addresses.
*   **Asynchronous Callbacks**: `qb::io::async::callback(func, delay)` for tasks like client reconnection.
*   **Thread-Safe Console I/O**: `qb::io::cout()`, `qb::io::cerr()`.

## How to Build and Run

1.  **Build**:
    Navigate to the main `build` directory of your QB framework checkout.
    Ensure CMake has been run from the root.
    Build the server and client targets:
    ```bash
    cmake --build . --target chat_server
    cmake --build . --target chat_client
    ```
    The executables will be found in `build/examples/core_io/chat_tcp/server/` and `build/examples/core_io/chat_tcp/client/`.

2.  **Run the Server**:
    Open a terminal and navigate to the server executable's directory.
    Start the server:
    ```bash
    ./chat_server
    ```
    The server will start listening on ports 3001 and 3002 by default.

3.  **Run the Client(s)**:
    Open one or more new terminals and navigate to the client executable's directory.
    Start a client, providing the host, port, and a username:
    ```bash
    ./chat_client <host> <port> <username>
    ```
    Example:
    ```bash
    ./chat_client 127.0.0.1 3001 Alice
    ./chat_client 127.0.0.1 3002 Bob
    ```
    Clients can connect to either port 3001 or 3002. Once connected, type messages and press Enter to send. Type "quit" to disconnect a client.

4.  **Shutdown**:
    To stop the server, press Enter in the terminal where the server is running. 