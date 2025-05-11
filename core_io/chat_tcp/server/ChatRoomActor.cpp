/**
 * @file examples/core_io/chat_tcp/server/ChatRoomActor.cpp
 * @example TCP Chat Server - Central Chat Room Logic Implementation
 * @brief Implements the `ChatRoomActor` which manages chat room state, user
 * authentication, and message broadcasting for the TCP chat server.
 *
 * @details
 * This file provides the implementation for the `ChatRoomActor`.
 * - `onInit()`: Registers handlers for `AuthEvent`, `ChatEvent`, and `DisconnectEvent`.
 * - `on(AuthEvent&)`: Handles new user authentication. It checks if a username is taken.
 *   If not, it stores the user's session ID and username, then sends a welcome message
 *   to the user (via `SendMessageEvent` to the relevant `ServerActor`) and broadcasts
 *   a "user has joined" message to all other users.
 * - `on(ChatEvent&)`: Processes incoming chat messages. It retrieves the sender's username
 *   using the session ID and then broadcasts the formatted message (e.g., "Username: message")
 *   to all connected clients.
 * - `on(DisconnectEvent&)`: Handles user disconnections. It removes the user from its internal
 *   tracking maps (`_sessions`, `_usernames`) and broadcasts a "user has left" message.
 * - `sendToSession()`: A helper method to send a `chat::Message` to a specific client session
 *   by creating a `SendMessageEvent` and `push`ing it to the `ServerActor` responsible for that session.
 * - `sendError()`: A utility to send an error message to a specific client.
 * - `broadcastMessage()`: A helper to create a `chat::Message` and send it to all currently
 *   active sessions via their respective `ServerActor`s.
 *
 * QB Features Demonstrated (in context of this implementation):
 * - `qb::Actor` methods for event handling and lifecycle.
 * - `push<Event>(destination, args...)` for sending events to other actors.
 * - Managing application state (user sessions, usernames) within an actor.
 * - Implementing application-specific logic (authentication, message formatting, broadcasting).
 * - `qb::io::cout`: Thread-safe console output for logging.
 */

#include "ChatRoomActor.h"
#include <iostream>

/**
 * @brief Initializes the chat room actor and registers event handlers
 * 
 * Sets up handlers for:
 * - Authentication events (user join/registration)
 * - Chat message events (message broadcasting)
 * - Disconnect events (user departure handling)
 * 
 * @return true if initialization succeeds, false otherwise
 */
bool ChatRoomActor::onInit() {
    registerEvent<AuthEvent>(*this);
    registerEvent<ChatEvent>(*this);
    registerEvent<DisconnectEvent>(*this);
    qb::io::cout() << "ChatRoomActor initialized with ID: " << id() << std::endl;
    return true;
}

/**
 * @brief Handles user authentication and registration
 * 
 * Authentication flow:
 * 1. Validates username uniqueness
 * 2. Registers user in session and username maps
 * 3. Sends welcome message to the user
 * 4. Broadcasts join notification to all users
 * 
 * @param evt The authentication event containing session details
 */
void ChatRoomActor::on(AuthEvent& evt) {
    auto session_id = evt.session_id;
    auto username = evt.username;
    auto server_id = evt.getSource();

    // Check if username is already taken
    if (_usernames.find(username) != _usernames.end()) {
        sendError(session_id, server_id, "Username already taken");
        return;
    }

    // Register the user
    _sessions[session_id] = SessionInfo{server_id, username};
    _usernames[username] = session_id;

    // Send confirmation
    chat::Message response;
    response.type = chat::MessageType::AUTH_RESPONSE;
    response.payload = "Welcome " + std::string(username);
    sendToSession(session_id, server_id, response);

    // Broadcast arrival
    broadcastMessage(std::string(username) + " has joined the chat");
}

/**
 * @brief Processes and broadcasts chat messages
 * 
 * Message handling:
 * 1. Validates sender's session
 * 2. Formats message with sender's username
 * 3. Broadcasts to all connected sessions
 * 
 * @param evt The chat event containing the message
 */
void ChatRoomActor::on(ChatEvent& evt) {
    auto session_id = evt.session_id;
    auto it = _sessions.find(session_id);
    if (it == _sessions.end()) return;

    auto& info = it->second;
    broadcastMessage(info.username + ": " + std::string(evt.message));
}

/**
 * @brief Manages user disconnection and cleanup
 * 
 * Disconnection flow:
 * 1. Removes user from session tracking
 * 2. Frees up username
 * 3. Notifies other users of departure
 * 
 * @param evt The disconnect event for the session
 */
void ChatRoomActor::on(DisconnectEvent& evt) {
    auto session_id = evt.session_id;
    auto it = _sessions.find(session_id);
    if (it == _sessions.end()) return;

    auto username = it->second.username;
    _usernames.erase(username);
    _sessions.erase(it);

    // Broadcast departure
    broadcastMessage(username + " has left the chat");
}

/**
 * @brief Sends a message to a specific session
 * 
 * Routes messages through the appropriate server actor
 * using QB's event system.
 * 
 * @param session_id Target session's unique identifier
 * @param server_id Server actor handling the session
 * @param msg The message to send
 */
void ChatRoomActor::sendToSession(qb::uuid session_id, qb::ActorId server_id, const chat::Message& msg) {
    auto& evt = push<SendMessageEvent>(server_id);
    evt.session_id = session_id;
    evt.message = msg;
}

/**
 * @brief Sends an error message to a specific session
 * 
 * Creates and routes an error message through the chat protocol.
 * 
 * @param session_id Target session's unique identifier
 * @param server_id Server actor handling the session
 * @param error The error message to send
 */
void ChatRoomActor::sendError(qb::uuid session_id, qb::ActorId server_id, const std::string& error) {
    chat::Message msg;
    msg.type = chat::MessageType::ERROR;
    msg.payload = error;
    sendToSession(session_id, server_id, msg);
}

/**
 * @brief Broadcasts a message to all connected sessions
 * 
 * Distribution pattern:
 * 1. Creates a chat message from content
 * 2. Iterates through all active sessions
 * 3. Routes message to each session via their server
 * 
 * @param content The message content to broadcast
 */
void ChatRoomActor::broadcastMessage(const std::string& content) {
    chat::Message msg;
    msg.type = chat::MessageType::CHAT_MESSAGE;
    msg.payload = content;

    for (const auto& [session_id, info] : _sessions) {
        sendToSession(session_id, info.server_id, msg);
    }
} 