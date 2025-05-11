/**
 * @file examples/io/example4_udp_networking.cpp
 * @example Asynchronous UDP Client-Server Communication
 *
 * @brief This example demonstrates basic User Datagram Protocol (UDP) networking
 * using QB-IO's asynchronous framework. It sets up a UDP server that echoes
 * received messages and a UDP client that sends messages and processes responses.
 *
 * @details
 * The example features two main classes:
 * 1.  `UDPServer`:
 *     -   Inherits from `qb::io::use<UDPServer>::udp::server`.
 *     -   Binds to a specific port using `transport().bind_v4()`.
 *     -   Uses `qb::protocol::text::command<UDPServer>` to interpret incoming datagrams
 *         as newline-terminated text messages.
 *     -   The `on(Protocol::message&& msg)` handler is invoked for each received message.
 *     -   It sends an echo response back to the client. The UDP server infrastructure
 *         implicitly handles sending the response to the source endpoint of the received datagram.
 *     -   Calls `start()` to begin listening for and processing datagrams.
 * 2.  `UDPClient`:
 *     -   Inherits from `qb::io::use<UDPClient>::udp::client`.
 *     -   Initializes its underlying socket using `transport().init()`.
 *     -   Uses `setDestination(qb::io::endpoint().as_in(host, port))` to specify the
 *         server's address before sending each datagram.
 *     -   Also uses `qb::protocol::text::command<UDPClient>` for message framing.
 *     -   Sends a series of messages to the server and processes responses in its
 *         `on(Protocol::message&& msg)` handler.
 *     -   Calls `start()` to enable receiving responses.
 *
 * The `main` function:
 * - Initializes the QB asynchronous I/O system using `qb::io::async::init()` for both server (main thread)
 *   and client (separate thread).
 * - Runs the `UDPServer` in the main thread and the `UDPClient` in a new thread.
 * - Both client and server use `qb::io::async::run(EVRUN_NOWAIT)` within loops to process
 *   I/O events asynchronously.
 * - Atomic counters (`server_received`, `client_received`) are used to track message flow
 *   and determine when the example can conclude.
 *
 * QB-IO Features Demonstrated:
 * - Asynchronous UDP Server: `qb::io::use<T>::udp::server`, `transport().bind_v4()`.
 * - Asynchronous UDP Client: `qb::io::use<T>::udp::client`, `transport().init()`, `setDestination()`.
 * - Endpoint Management: `qb::io::endpoint` for specifying destination addresses.
 * - Built-in Text Protocol over UDP: `qb::protocol::text::command<HandlerType>`.
 * - Asynchronous Event Loop: `qb::io::async::init()`, `qb::io::async::run()`.
 * - Thread-Safe Output: `qb::io::cout()` and `qb::io::cerr()`.
 */

#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <thread>
#include <atomic>
#include <iostream>

constexpr const unsigned short SERVER_PORT = 9090;
constexpr const char* TEST_MESSAGE = "Hello from UDP client!";
constexpr const size_t MESSAGE_COUNT = 5;

// Atomic counters for tracking messages
std::atomic<std::size_t> server_received{0};
std::atomic<std::size_t> client_received{0};

// Check if client has received all responses
bool client_done() {
    return client_received >= MESSAGE_COUNT;
}

// Check if server has received all messages
bool server_done() {
    return server_received >= MESSAGE_COUNT;
}

// UDP Server implementation
class UDPServer : public qb::io::use<UDPServer>::udp::server {
public:
    // Define the protocol to use for parsing messages
    using Protocol = qb::protocol::text::command<UDPServer>;

    UDPServer() = default;

    ~UDPServer() {
        qb::io::cout() << "UDP Server destroyed. Received " << server_received << " messages." << std::endl;
    }

    // Handler for received messages
    void on(Protocol::message&& msg) {
        qb::io::cout() << "Server received: " << msg.text << std::endl;
        
        // Echo the message back to the client
        *this << "Response to: " << msg.text << Protocol::end;
        
        // Increment message counter
        ++server_received;
    }
};

// UDP Client implementation
class UDPClient : public qb::io::use<UDPClient>::udp::client {
public:
    // Define the protocol to use for parsing messages
    using Protocol = qb::protocol::text::command<UDPClient>;

    UDPClient() = default;

    ~UDPClient() {
        qb::io::cout() << "UDP Client destroyed. Received " << client_received << " responses." << std::endl;
    }

    // Handler for received messages
    void on(Protocol::message&& msg) {
        qb::io::cout() << "Client received: " << msg.text << std::endl;
        
        // Increment response counter
        ++client_received;
    }
};

int main() {
    // Initialize async system
    qb::io::async::init();
    
    // Create and start the UDP server
    UDPServer server;
    if (server.transport().bind_v4(SERVER_PORT)) {
        qb::io::cerr() << "Failed to bind server to port " << SERVER_PORT << std::endl;
        return 1;
    }
    server.start();
    qb::io::cout() << "UDP Server started on port " << SERVER_PORT << std::endl;
    
    // Create and run the client in a separate thread
    std::thread client_thread([]() {
        // Initialize async system in this thread
        qb::io::async::init();
        
        // Create the UDP client
        UDPClient client;
        
        // Initialize the client transport
        client.transport().init();
        if (!client.transport().is_open()) {
            qb::io::cerr() << "Failed to initialize client socket" << std::endl;
            return;
        }
        
        // Start the client
        client.start();
        qb::io::cout() << "UDP Client started" << std::endl;
        
        // Send messages to the server
        for (size_t i = 0; i < MESSAGE_COUNT; ++i) {
            // Set the destination to the server
            client.setDestination(qb::io::endpoint().as_in("127.0.0.1", SERVER_PORT));
            
            // Create the message with a sequence number
            std::string message = std::string(TEST_MESSAGE) + " #" + std::to_string(i+1);
            
            // Send the message using the protocol's stream syntax
            client << message << UDPClient::Protocol::end;
            qb::io::cout() << "Client sent: " << message << std::endl;
            
            // Process events to avoid backup
            qb::io::async::run(EVRUN_NOWAIT);
            
            // Small delay between messages
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Wait for all responses or timeout
        qb::io::cout() << "Client waiting for responses..." << std::endl;
        for (int i = 0; i < 100 && !client_done(); ++i) {
            qb::io::async::run(EVRUN_NOWAIT);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    // Process server events until all expected messages are received
    qb::io::cout() << "Server processing events..." << std::endl;
    for (int i = 0; i < 100 && (!server_done() || !client_done()); ++i) {
        qb::io::async::run(EVRUN_NOWAIT);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Wait for client thread to finish
    client_thread.join();
    
    // Print results
    qb::io::cout() << "Example completed: " << std::endl
                  << "  - Server received: " << server_received << " messages" << std::endl
                  << "  - Client received: " << client_received << " responses" << std::endl;
    
    return 0;
} 