/**
 * @file examples/core_io/chat_tcp/server/ChatSession.h
 * @example TCP Chat Server - Client Session Handler
 * @brief Defines the `ChatSession` class, responsible for managing an individual
 * client connection on the server side.
 *
 * @details
 * `ChatSession` represents a single connected client within a `ServerActor`.
 * It handles the I/O and protocol parsing for that specific client.
 * Key responsibilities and features:
 * - Inherits from `qb::io::use<ChatSession>::tcp::client<ServerActor>` to act as the server-side
 *   endpoint for a client's TCP connection, associating it with its managing `ServerActor`.
 * - Inherits from `qb::io::use<ChatSession>::timeout` to implement session inactivity timeouts.
 * - Uses the custom `ChatProtocol` (defined in `shared/Protocol.h`) for message framing
 *   and parsing by specifying `using Protocol = chat::ChatProtocol<ChatSession>;`
 *   and calling `this->template switch_protocol<Protocol>(*this);`.
 * - `on(const chat::Message& msg)`: Handles fully parsed messages from the client.
 *   It forwards authentication requests and chat messages to the managing `ServerActor`.
 * - `on(qb::io::async::event::disconnected const &)`: Handles client disconnection events,
 *   notifying the `ServerActor`.
 * - `on(qb::io::async::event::timer const &)`: Handles session timeout events by disconnecting the client.
 * - `updateTimeout()`: Resets the inactivity timer upon receiving client activity.
 *
 * This class demonstrates how QB-IO's composable `use` templates can be combined to build
 * sophisticated network session handlers with features like custom protocols and timeouts.
 *
 * QB Features Demonstrated:
 * - `qb::io::use<ChatSession>::tcp::client<ServerActor>`: Server-side representation of a client connection,
 *   linked to a `ServerActor` (its `IOServer`).
 * - `qb::io::use<ChatSession>::timeout`: Mixin for session inactivity timeout.
 *   - `setTimeout(seconds)`: Configures the timeout duration.
 *   - `updateTimeout()`: Resets the timer.
 *   - `on(qb::io::async::event::timer const &)`: Timeout event handler.
 * - Custom Protocol Integration: Using `ChatProtocol` derived from `qb::io::async::AProtocol`.
 *   - `switch_protocol<Protocol>(*this)`: Activates the protocol for the session.
 *   - `on(const chat::Message& msg)`: Receives messages parsed by the protocol.
 * - QB Event Handling: `on(qb::io::async::event::disconnected const&)`.
 * - Interaction with Parent Actor: Accessing `this->server()` (the `ServerActor`) to delegate tasks.
 */

#pragma once

#include <qb/io/async.h>
#include "../shared/Protocol.h"

class ServerActor;

/**
 * @brief Handles individual client connections in the chat server
 * 
 * ChatSession demonstrates several QB I/O features:
 * 1. TCP Client Functionality:
 *    - Inherits from qb::io::use<ChatSession>::tcp::client<ServerActor>
 *    - Provides asynchronous message reading/writing
 *    - Manages connection state
 * 
 * 2. Timeout Management:
 *    - Inherits from qb::io::use<ChatSession>::timeout
 *    - Automatically disconnects inactive clients
 *    - Demonstrates QB's composable I/O features
 * 
 * 3. Protocol Integration:
 *    - Uses ChatProtocol for message parsing
 *    - Shows how to integrate custom protocols with QB I/O
 *    - Handles protocol-level events
 * 
 * 4. Event Routing:
 *    - Routes protocol messages to ServerActor
 *    - Handles connection events
 *    - Manages session lifecycle events
 */
class ChatSession : public qb::io::use<ChatSession>::tcp::client<ServerActor>,
                    public qb::io::use<ChatSession>::timeout {
public:
    /**
     * @brief Protocol type used by this session
     * 
     * QB's I/O framework uses this typedef to:
     * 1. Set up message parsing
     * 2. Configure protocol handlers
     * 3. Route protocol events correctly
     */
    using Protocol = chat::ChatProtocol<ChatSession>;
    
    /**
     * @brief Constructs a new chat session
     * 
     * Demonstrates QB session initialization:
     * 1. Calls parent constructor with server reference
     * 2. Sets up protocol handling
     * 3. Configures timeout
     * 4. Initializes session state
     * 
     * @param server Reference to the owning ServerActor for event routing
     */
    explicit ChatSession(ServerActor& server);
    
    /**
     * @brief Destructor
     * 
     * QB calls this when:
     * 1. Client disconnects
     * 2. Session times out
     * 3. Server shuts down
     * 
     * Used for cleanup and logging.
     */
    ~ChatSession();
    
    /**
     * @brief Handles incoming chat protocol messages
     * 
     * QB's protocol framework calls this when:
     * 1. A complete message is received
     * 2. The message has been parsed
     * 3. Protocol validation succeeds
     * 
     * Routes different message types to appropriate ServerActor handlers.
     * 
     * @param msg The validated and parsed message
     */
    void on(const chat::Message& msg);
    
    /**
     * @brief Handles client disconnection
     * 
     * QB's I/O framework calls this when:
     * 1. Client closes connection
     * 2. Network error occurs
     * 3. Session is explicitly closed
     * 
     * Ensures proper cleanup by notifying ServerActor.
     * 
     * @param evt The QB disconnection event
     */
    void on(qb::io::async::event::disconnected const &);

    /**
     * @brief Handles session timeout
     * 
     * QB's timeout system calls this when:
     * 1. No activity for specified duration
     * 2. No messages received
     * 3. No keep-alive received
     * 
     * Demonstrates QB's automatic resource management.
     * 
     * @param timer The QB timer event
     */
    void on(qb::io::async::event::timer const &);
};