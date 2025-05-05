/**
 * @file ServerActor.cpp
 * @brief Implementation demonstrating QB's server-side patterns
 * 
 * This file demonstrates:
 * 1. How to implement a QB server actor
 * 2. How to manage multiple client sessions
 * 3. How to handle inter-actor communication
 * 4. How to integrate with QB's I/O framework
 */

#include "ServerActor.h"
#include "ChatSession.h"
#include "../shared/Events.h"
#include <iostream>

/**
 * Constructor showing QB actor initialization:
 * - Minimal construction pattern
 * - Stores dependencies for later use
 * - Prepares for session management
 */
ServerActor::ServerActor(qb::ActorId chatroom_id)
    : _chatroom_id(chatroom_id) {}

/**
 * Initialization demonstrating QB's event registration:
 * 1. Event Setup:
 *    - Registers NewSessionEvent for new connections
 *    - Registers SendMessageEvent for message routing
 * 
 * 2. Actor Preparation:
 *    - Sets up event handlers
 *    - Initializes internal state
 *    - Enables logging for monitoring
 */
bool ServerActor::onInit() {
    registerEvent<NewSessionEvent>(*this);
    registerEvent<SendMessageEvent>(*this);
    std::cout << "ServerActor initialized with ID: " << id() << std::endl;
    return true;
}

/**
 * Session creation handler showing QB's connection management:
 * 1. Socket Handling:
 *    - Receives connected socket
 *    - Creates new session
 *    - Sets up protocol handling
 * 
 * 2. Session Management:
 *    - Registers session in io_handler
 *    - Initializes session state
 *    - Enables monitoring
 */
void ServerActor::on(NewSessionEvent& evt) {
    // Create and register a new chat session for the incoming connection
    auto& session = registerSession(std::move(evt.socket));
    std::cout << "New session registered: " << session.id() << std::endl;
}

/**
 * Authentication handler showing QB's event routing:
 * 1. Event Creation:
 *    - Creates AuthEvent for ChatRoom
 *    - Sets session and username
 *    - Routes to correct actor
 * 
 * 2. State Management:
 *    - Maintains session mapping
 *    - Tracks authentication state
 *    - Enables user management
 */
void ServerActor::handleAuth(qb::uuid session_id, const std::string& username) {
    // Forward authentication request to ChatRoomActor
    auto& evt = push<AuthEvent>(_chatroom_id);
    evt.session_id = session_id;
    evt.username = username;
}

/**
 * Chat message handler showing QB's message routing:
 * 1. Message Processing:
 *    - Creates ChatEvent
 *    - Sets message content
 *    - Routes to ChatRoom
 * 
 * 2. Session Validation:
 *    - Verifies session exists
 *    - Ensures proper routing
 *    - Maintains message flow
 */
void ServerActor::handleChat(qb::uuid session_id, const std::string& message) {
    // Forward chat message to ChatRoomActor for broadcasting
    auto& evt = push<ChatEvent>(_chatroom_id);
    evt.session_id = session_id;
    evt.message = message;
}

/**
 * Disconnection handler showing QB's cleanup patterns:
 * 1. Session Cleanup:
 *    - Notifies ChatRoom
 *    - Removes session state
 *    - Updates user tracking
 * 
 * 2. Resource Management:
 *    - Cleans up session resources
 *    - Updates system state
 *    - Maintains consistency
 */
void ServerActor::handleDisconnect(qb::uuid session_id) {
    // Notify ChatRoomActor about client disconnection
    auto& evt = push<DisconnectEvent>(_chatroom_id);
    evt.session_id = session_id;
}

/**
 * Message delivery handler showing QB's session communication:
 * 1. Message Routing:
 *    - Finds target session
 *    - Validates session state
 *    - Delivers message
 * 
 * 2. Error Handling:
 *    - Handles missing sessions
 *    - Ensures delivery
 *    - Maintains system stability
 */
void ServerActor::on(SendMessageEvent& evt) {
    // Send message to specific session if it exists
    auto it = sessions().find(evt.session_id);
    if (it != sessions().end()) {
        *it->second << evt.message; // Send message to session
        it->second->updateTimeout(); // Update session timeout to prevent disconnection
    }
} 