/**
 * @file ServerActor.h
 * @brief Server actor that manages chat sessions and bridges them with ChatRoomActor
 * 
 * This file demonstrates:
 * 1. How to combine QB Actor system with I/O handling
 * 2. How to manage multiple client sessions
 * 3. How to bridge communication between different actor types
 * 4. Proper event handling and routing in a QB application
 */
#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include "../shared/Protocol.h"
#include "../shared/Events.h"
#include "ChatSession.h"

/**
 * @brief Manages a pool of chat sessions and bridges them with ChatRoomActor
 * 
 * ServerActor demonstrates several key QB framework concepts:
 * 1. Multiple inheritance from QB base classes:
 *    - qb::Actor for actor system integration
 *    - qb::io::use<ServerActor>::tcp::io_handler<ChatSession> for I/O handling
 * 
 * 2. Session Management:
 *    - Creates ChatSession instances for new connections
 *    - Manages session lifecycle (creation, message handling, cleanup)
 *    - Routes messages between sessions and ChatRoomActor
 * 
 * 3. Event Handling:
 *    - Handles NewSessionEvent from AcceptActor
 *    - Handles SendMessageEvent from ChatRoomActor
 *    - Routes session events to ChatRoomActor
 * 
 * 4. QB I/O Integration:
 *    - Uses QB's I/O framework for TCP communication
 *    - Demonstrates proper session management patterns
 *    - Shows how to handle async I/O in an actor system
 */
class ServerActor : public qb::Actor,
                    public qb::io::use<ServerActor>::tcp::io_handler<ChatSession> {
private:
    qb::ActorId _chatroom_id;  // ID of the ChatRoom actor for message routing

public:
    /**
     * @brief Constructs a new server actor
     * 
     * @param chatroom_id ID of the ChatRoom actor this server will work with
     * 
     * The ServerActor needs the ChatRoom's ID to:
     * 1. Forward authentication requests
     * 2. Route chat messages
     * 3. Notify about client disconnections
     */
    explicit ServerActor(qb::ActorId chatroom_id);
    
    /**
     * @brief Initializes the server actor
     * 
     * QB Framework calls this when the actor starts. This method:
     * 1. Registers necessary event handlers
     * 2. Sets up any required resources
     * 3. Logs initialization for debugging
     * 
     * @return true if initialization successful, false otherwise
     */
    bool onInit() override;

    /**
     * @brief Handles new client connections
     * 
     * Called by AcceptActor when a new client connects. This method:
     * 1. Creates a new ChatSession for the client
     * 2. Initializes the session with the received socket
     * 3. Sets up protocol handling for the session
     * 
     * @param evt Event containing the new client's socket
     * 
     * This demonstrates QB's session management pattern where
     * io_handler creates and manages session instances.
     */
    void on(NewSessionEvent& evt);
    
    /**
     * @brief Handles authentication requests from sessions
     * 
     * Called by ChatSession when a client sends an AUTH_REQUEST.
     * Demonstrates proper event routing in QB:
     * 1. Creates an AuthEvent
     * 2. Sets necessary event fields
     * 3. Pushes event to ChatRoomActor
     * 
     * @param session_id ID of the requesting session
     * @param username Requested username
     */
    void handleAuth(qb::uuid session_id, const std::string& username);

    /**
     * @brief Handles chat messages from sessions
     * 
     * Called by ChatSession when a client sends a CHAT_MESSAGE.
     * Shows QB's event-based communication:
     * 1. Creates a ChatEvent
     * 2. Fills event data
     * 3. Routes to ChatRoomActor for broadcasting
     * 
     * @param session_id ID of the sending session
     * @param message The chat message content
     */
    void handleChat(qb::uuid session_id, const std::string& message);

    /**
     * @brief Handles session disconnections
     * 
     * Called by ChatSession when a client disconnects.
     * Demonstrates cleanup pattern in QB:
     * 1. Notifies ChatRoomActor via DisconnectEvent
     * 2. Allows for proper user cleanup
     * 3. Maintains system consistency
     * 
     * @param session_id ID of the disconnected session
     */
    void handleDisconnect(qb::uuid session_id);
    
    /**
     * @brief Handles message delivery requests from ChatRoom
     * 
     * Called when ChatRoom wants to send a message to a specific client.
     * Shows QB's bidirectional communication:
     * 1. Receives SendMessageEvent from ChatRoom
     * 2. Looks up target session
     * 3. Delivers message using QB's I/O system
     * 
     * @param evt Event containing the target session and message
     */
    void on(SendMessageEvent& evt);
}; 