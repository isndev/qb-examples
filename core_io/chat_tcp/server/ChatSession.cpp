/**
 * @file examples/core_io/chat_tcp/server/ChatSession.cpp
 * @example TCP Chat Server - Client Session Implementation
 * @brief Implements the `ChatSession` class for handling individual client
 * connections in the TCP chat server.
 *
 * @details
 * This file provides the implementation for `ChatSession`.
 * - Constructor: Initializes the base `client` with the managing `ServerActor`,
 *   switches to the `ChatProtocol`, and sets an inactivity timeout.
 * - Destructor: Logs client disconnection.
 * - `on(const chat::Message& msg)`: This is the handler for messages successfully parsed
 *   by `ChatProtocol`. It inspects the `msg.type` and delegates processing to the
 *   parent `ServerActor` (`this->server()`) by calling its `handleAuth` or `handleChat` methods.
 *   It also calls `this->updateTimeout()` to reset the inactivity timer upon message receipt.
 * - `on(qb::io::async::event::disconnected const &)`: Called by QB-IO when the client TCP connection
 *   is lost. It notifies the parent `ServerActor` by calling `this->server().handleDisconnect()`.
 * - `on(qb::io::async::event::timer const &)`: Called by the `qb::io::use<...>::timeout` mixin
 *   when the session has been inactive for the configured duration. It initiates a client
 *   disconnection by calling `this->disconnect()` (a method from the `tcp::client` base).
 *
 * QB Features Demonstrated (in context of this implementation):
 * - `qb::io::use<...>::tcp::client<ServerActor>` base class usage and initialization.
 * - `switch_protocol<Protocol>(*this)` to apply a custom protocol.
 * - `setTimeout(duration)` and `updateTimeout()` from `qb::io::use<...>::timeout`.
 * - Handling protocol messages: `on(const chat::Message& msg)`.
 * - Handling I/O events: `on(qb::io::async::event::disconnected const &)`, `on(qb::io::async::event::timer const &)`.
 * - Delegating logic to a parent/managing actor (`this->server()`).
 * - `qb::io::cout`, `qb::io::cerr`: Thread-safe console output.
 */

#include "ChatSession.h"
#include "ServerActor.h"
#include <iostream>

/**
 * Session constructor showing QB initialization sequence:
 * 1. Base constructor sets up TCP client functionality
 * 2. Protocol switching configures message handling
 * 3. Timeout setup ensures resource cleanup
 * 4. Logging helps with debugging and monitoring
 */
ChatSession::ChatSession(ServerActor& server)
    : client(server) {
    // Set up protocol handler and timeout
    this->template switch_protocol<Protocol>(*this);
    this->setTimeout(120);  // 120 second timeout
    qb::io::cout() << "New chat client connected" << std::endl;
}

/**
 * Destructor demonstrating QB cleanup pattern:
 * - Called on normal disconnection
 * - Called on timeout
 * - Called on server shutdown
 * - Ensures proper resource cleanup
 */
ChatSession::~ChatSession() {
    qb::io::cout() << "Chat client disconnected" << std::endl;
}

/**
 * Message handler showing QB's protocol integration:
 * 1. Protocol Framework:
 *    - Automatically parses incoming data
 *    - Validates message format
 *    - Routes to appropriate handler
 * 
 * 2. Message Routing:
 *    - AUTH_REQUEST: Forward to authentication handler
 *    - CHAT_MESSAGE: Forward to chat handler
 *    - Handles unknown message types
 * 
 * 3. Server Integration:
 *    - Uses server() to access parent
 *    - Maintains proper actor hierarchy
 *    - Routes messages to correct handlers
 */
void ChatSession::on(const chat::Message& msg) {
    // Route incoming messages to appropriate handlers in ServerActor
    switch(msg.type) {
        case chat::MessageType::AUTH_REQUEST:
            this->server().handleAuth(this->id(), msg.payload);
            break;
        case chat::MessageType::CHAT_MESSAGE:
            this->server().handleChat(this->id(), msg.payload);
            break;
        default:
            qb::io::cerr() << "Unknown message type: " << static_cast<int>(msg.type) << std::endl;
            break;
    }
    this->updateTimeout(); // Update timeout to prevent disconnection
}

/**
 * Disconnection handler showing QB's event system:
 * 1. Event Handling:
 *    - Automatic detection of disconnection
 *    - Clean notification system
 *    - Resource management
 * 
 * 2. Server Notification:
 *    - Informs server of disconnection
 *    - Allows for user cleanup
 *    - Maintains system consistency
 */
void ChatSession::on(qb::io::async::event::disconnected const &) {
    // Notify server when client disconnects
    qb::io::cout() << "Chat client disconnected" << std::endl;
    this->server().handleDisconnect(this->id());
}

/**
 * Timeout handler demonstrating QB's timeout management:
 * 1. Automatic Detection:
 *    - QB tracks session activity
 *    - Triggers on inactivity
 *    - Configurable duration
 * 
 * 2. Resource Protection:
 *    - Prevents resource leaks
 *    - Cleans up inactive sessions
 *    - Maintains system stability
 */
void ChatSession::on(qb::io::async::event::timer const &) {
    // Handle session timeout by disconnecting the client
    qb::io::cout() << "Chat client timed out" << std::endl;
    this->disconnect();
} 