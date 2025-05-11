# QB-IO Library Examples

This directory contains examples demonstrating various features of the QB-IO library, which forms the asynchronous I/O and utilities foundation for the QB C++ Actor Framework. These examples are designed to run as standalone applications and illustrate how to use `qb-io` components directly.

## Building the Examples

To build these examples, navigate to the root of the QB Framework and use CMake:

```bash
# From the root directory of the qb-framework
mkdir build && cd build
cmake .. 
# To build a specific IO example (e.g., example1_async_io)
cmake --build . --target example1_async_io
# Or build all examples
cmake --build . 
```
The executables will be located in the `build/examples/io/` directory.

## Examples Overview

Below is a detailed explanation of each example within this directory.

---

### 1. Asynchronous I/O Fundamentals (`example1_async_io.cpp`)

*   **@example Asynchronous I/O Fundamentals with QB-IO**
*   **Brief**: Demonstrates core asynchronous capabilities of the QB-IO library, including timer-driven operations, basic file I/O, and file system event watching. It is designed to run as a standalone application, showcasing `qb-io` features outside of the full `qb-core` actor system.
*   **Details**:
    *   **`FileProcessor`**:
        *   Uses `qb::io::async::with_timeout<FileProcessor>` for periodic actions.
        *   Alternates writing to and reading from `qb_io_example.txt` using synchronous `qb::io::system::file` calls, triggered by asynchronous timer events.
        *   Reschedules itself using `updateTimeout()`.
    *   **`TimerDemonstration`**:
        *   Also uses `qb::io::async::with_timeout<TimerDemonstration>`.
        *   Shows a simple recurring timer logging "tick" messages.
    *   **`FileWatcher`**:
        *   Uses `ev::stat` (from `libev`, integrated with QB-IO's event loop) to monitor `qb_io_example.txt` for attribute changes (size, modification time).
    *   **`main` function**:
        *   Initializes the QB async I/O system (`qb::io::async::init()`).
        *   Sets up signal handling (Ctrl+C).
        *   Creates and runs `FileProcessor`, `TimerDemonstration`, and `FileWatcher`.
        *   Drives the event loop using `qb::io::async::run(EVRUN_NOWAIT)` for a set duration.
        *   Cleans up the test file.
*   **QB-IO Features Demonstrated**:
    *   Asynchronous System Initialization: `qb::io::async::init()`.
    *   Event Loop Integration: `qb::io::async::run()`.
    *   Timer-Based Operations: `qb::io::async::with_timeout<T>`, `on(qb::io::async::event::timer const&)`, `updateTimeout()`.
    *   Basic File I/O: `qb::io::system::file` for synchronous file operations, orchestrated asynchronously.
    *   Low-Level File Watching: Direct use of `ev::stat` with `qb::io::async::listener::current.loop()`.
    *   Thread-Safe Output: `qb::io::cout()`, `qb::io::cerr()`.
*   **To Run**:
    ```bash
    ./build/examples/io/example1_async_io
    ```
    Observe the console output for file operations, timer ticks, and file change notifications. The program will run for a few seconds and then clean up.

---

### 2. File I/O Operations (`example2_file_io.cpp`)

*   **@example Comprehensive File I/O Operations with QB-IO and POSIX**
*   **Brief**: Demonstrates various file I/O operations, using `qb::io::system::file` for basic tasks and standard C++/POSIX calls for memory-mapped files and file statistics.
*   **Details**:
    *   The `FileOperationsManager` class orchestrates:
        1.  **Writing Binary File**: Generates random binary data and writes it using `qb::io::system::file`. Measures throughput.
        2.  **Reading Binary File**: Reads the file back using `qb::io::system::file`. Measures throughput.
        3.  **Memory-Mapped I/O**: Uses POSIX `mmap()` to map a file to memory, writes data directly to the mapped region, and syncs with `msync()`.
        4.  **File Copy**: Copies a file in chunks using `qb::io::system::file`.
        5.  **File Information**: Retrieves file metadata (size, permissions, mtime) using POSIX `stat()`.
    *   The example focuses on synchronous file operations and their performance.
*   **QB-IO Features Demonstrated**:
    *   Synchronous File Operations: `qb::io::system::file` for `open()`, `read()`, `write()`, and `close()`.
    *   Thread-Safe Output: `qb::io::cout()`, `qb::io::cerr()`.
*   **Other POSIX/Standard C++ Features Shown**:
    *   Memory-Mapped Files: `mmap`, `munmap`, `msync`, `ftruncate` (POSIX).
    *   File Statistics: `stat` (POSIX).
    *   Filesystem Operations: `std::filesystem`.
*   **To Run**:
    ```bash
    ./build/examples/io/example2_file_io
    ```
    The program will create a test directory (`qb_fileio_test`), perform operations, print results and throughput, and then clean up.

---

### 3. TCP Networking (`example3_tcp_networking.cpp`)

*   **@example Asynchronous TCP Client-Server Communication**
*   **Brief**: Demonstrates TCP networking with QB-IO, featuring an asynchronous server handling multiple clients and a client interacting via a text-based command protocol.
*   **Details**:
    *   **`TCPServer`**:
        *   Inherits `qb::io::use<TCPServer>::tcp::server<ServerClientHandler>`.
        *   Listens on a port (`transport().listen_v4()`).
        *   Creates a `ServerClientHandler` for each new connection.
    *   **`ServerClientHandler`** (Server-side session):
        *   Inherits `qb::io::use<ServerClientHandler>::tcp::client<TCPServer>`.
        *   Uses `qb::protocol::text::command` for newline-terminated command processing.
        *   Handles commands like "help", "time", "echo", "stats", "quit", "shutdown".
    *   **`TCPClient`**:
        *   Inherits `qb::io::use<TCPClient>::tcp::client<>`.
        *   Connects using `transport().connect_v4()`.
        *   Uses `qb::protocol::text::command` for communication.
        *   Sends a series of test commands.
    *   Server and client run in separate threads, using `qb::io::async::init()` and `qb::io::async::run(EVRUN_NOWAIT)`.
*   **QB-IO Features Demonstrated**:
    *   Asynchronous TCP Server: `qb::io::use<T>::tcp::server<SessionType>`, `transport().listen_v4()`.
    *   Asynchronous TCP Client: `qb::io::use<T>::tcp::client<>`, `transport().connect_v4()`.
    *   Session Management by Server.
    *   Built-in Text Protocol: `qb::protocol::text::command<HandlerType>`.
    *   Protocol Integration: `switch_protocol()` (implicit), `on(Protocol::message&&)`.
    *   Stream-Based Sending: `*this << message << Protocol::end;`.
    *   Asynchronous Event Loop: `qb::io::async::init()`, `qb::io::async::run()`.
*   **To Run**:
    ```bash
    ./build/examples/io/example3_tcp_networking
    ```
    The server will start, followed by the client which connects, sends commands, and then the system shuts down.

---

### 4. UDP Networking (`example4_udp_networking.cpp`)

*   **@example Asynchronous UDP Client-Server Communication**
*   **Brief**: Shows basic UDP client-server interaction using QB-IO's asynchronous framework, with a server echoing messages.
*   **Details**:
    *   **`UDPServer`**:
        *   Inherits `qb::io::use<UDPServer>::udp::server`.
        *   Binds to a port (`transport().bind_v4()`).
        *   Uses `qb::protocol::text::command` to parse datagrams.
        *   Echoes received messages back to the sender.
    *   **`UDPClient`**:
        *   Inherits `qb::io::use<UDPClient>::udp::client`.
        *   Initializes transport (`transport().init()`).
        *   Sets destination using `setDestination(qb::io::endpoint().as_in(host, port))`.
        *   Uses `qb::protocol::text::command` for messages.
        *   Sends multiple messages and awaits responses.
    *   Server runs in the main thread, client in a new thread. Both use `qb::io::async::init()` and `qb::io::async::run(EVRUN_NOWAIT)`.
*   **QB-IO Features Demonstrated**:
    *   Asynchronous UDP Server: `qb::io::use<T>::udp::server`, `transport().bind_v4()`.
    *   Asynchronous UDP Client: `qb::io::use<T>::udp::client`, `transport().init()`, `setDestination()`.
    *   Endpoint Management: `qb::io::endpoint`.
    *   Built-in Text Protocol over UDP: `qb::protocol::text::command<HandlerType>`.
    *   Asynchronous Event Loop.
*   **To Run**:
    ```bash
    ./build/examples/io/example4_udp_networking
    ```
    The client will send messages to the server, and both will print received data.

---

### 5. Custom Protocol (`example5_custom_protocol.cpp`)

*   **@example Custom Binary Protocol Implementation with QB-IO**
*   **Brief**: A comprehensive example of defining, implementing, and using a custom binary network protocol with QB-IO, including message framing, serialization, and parsing.
*   **Details**:
    *   **Protocol Definition (`qb_protocol` namespace)**:
        *   `MessageHeader`: 12-byte header (magic, version, type, id, payload length).
        *   `qb::custom_message`: Struct for parsed messages (type, id, payload string).
    *   **Serialization**:
        *   `qb::allocator::pipe<char>::put<qb::custom_message>` specialization serializes `qb::custom_message` into the binary format.
    *   **Deserialization/Parsing (`qb::custom_protocol<IO_>` class)**:
        *   Extends `qb::io::async::AProtocol<IO_>`.
        *   `getMessageSize()`: Reads and validates the header to determine full message size.
        *   `onMessage()`: Reconstructs `qb::custom_message` from buffer and dispatches it.
    *   **Server (`EchoServer`, `EchoServerClient`)**: Uses the custom protocol. Handles `HELLO` (sends `ACK`) and `ECHO_REQUEST` (sends `ECHO_REPLY`).
    *   **Client (`EchoClient`)**: Uses the custom protocol. Sends `HELLO`, then `ECHO_REQUEST`s based on interactive user input. Manages response callbacks with timeouts.
    *   `main` can run in server or client mode.
*   **QB-IO Features Demonstrated**:
    *   Custom Protocol Implementation: `qb::io::async::AProtocol<IO_>`.
    *   Message Framing & Parsing: `getMessageSize()`, `onMessage()`.
    *   Custom Serialization: `qb::allocator::pipe<char>::put<T>()`.
    *   Protocol Switching: `switch_protocol<MyCustomProtocol>()`.
    *   Asynchronous TCP Client/Server.
    *   Asynchronous Event Loop.
*   **To Run**:
    1.  Start the server:
        ```bash
        ./build/examples/io/example5_custom_protocol server 9876
        ```
    2.  In another terminal, start the client:
        ```bash
        ./build/examples/io/example5_custom_protocol client 127.0.0.1 9876
        ```
        The client will connect, send a HELLO message, and then enter an interactive mode where you can type messages to be echoed by the server. Type "quit" to exit the client.

---

This README provides a guide to understanding and running the QB-IO examples. Each example is self-contained and focuses on specific aspects of the library. 