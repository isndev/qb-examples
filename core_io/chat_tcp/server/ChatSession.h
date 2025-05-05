/**
 * @file ChatSession.h
 * @brief Session management for individual chat clients
 * 
 * This file demonstrates:
 * 1. How to implement client session handling in QB
 * 2. How to use QB's I/O framework for TCP communication
 * 3. How to handle protocol messages and timeouts
 * 4. How to integrate multiple QB I/O features
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