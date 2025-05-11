/**
 * @file examples/core_io/chat_tcp/server/ChatRoomActor.h
 * @example TCP Chat Server - Central Chat Room Logic Actor
 * @brief Defines the `ChatRoomActor` which manages the central state of the chat,
 * including user sessions, authentication, and message broadcasting.
 *
 * @details
 * The `ChatRoomActor` is a key component in the server-side architecture. Its main
 * responsibilities are:
 * - **Session Management**: Keeps track of all active client sessions (`_sessions` map)
 *   and their associated usernames (`_usernames` map for quick lookup).
 * - **Authentication**: Handles `AuthEvent`s sent by `ServerActor`s. It checks for username
 *   uniqueness, registers new users, and sends an authentication response.
 * - **Message Broadcasting**: Receives `ChatEvent`s (containing messages from clients)
 *   and broadcasts them to all other connected and authenticated clients by sending
 *   `SendMessageEvent`s to the appropriate `ServerActor`s.
 * - **Disconnection Handling**: Processes `DisconnectEvent`s to remove users from its
 *   tracking maps and notify other users of departures.
 *
 * It acts as a centralized point of coordination and state for the chat application logic,
 * ensuring that all participants see a consistent view of the chat room.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: For encapsulating state and behavior.
 * - Event Handling: `onInit()`, `on(AuthEvent&)`, `on(ChatEvent&)`, `on(DisconnectEvent&)`.
 * - Inter-Actor Communication: Receiving events from `ServerActor`s and sending `SendMessageEvent`s
 *   back to `ServerActor`s for delivery to specific clients.
 * - State Management: Using `std::map` to store session information and usernames.
 * - Centralized Logic: Implementing core application logic (authentication, broadcast) in one actor.
 */

#pragma once

#include <qb/actor.h>
#include <map>
#include <string>
#include "../shared/Protocol.h"
#include "../shared/Events.h"

/**
 * @brief Structure holding information about a connected session
 * 
 * Demonstrates QB's pattern for managing session state:
 * 1. Keeps minimal required information
 * 2. Uses value types for thread safety
 * 3. Maintains references to other actors
 */
struct SessionInfo {
    qb::ActorId server_id;  // ID of the server managing this session
    std::string username;   // Username associated with the session
};

/**
 * @brief Actor responsible for managing the chat room state and message distribution
 * 
 * ChatRoomActor demonstrates QB's state management patterns:
 * 1. Centralized State Management:
 *    - Maintains session and username mappings
 *    - Ensures consistency across the system
 *    - Handles concurrent access safely
 * 
 * 2. Event Processing:
 *    - Handles authentication requests
 *    - Processes chat messages
 *    - Manages user sessions
 * 
 * 3. Message Broadcasting:
 *    - Implements efficient message distribution
 *    - Routes messages to correct servers
 *    - Handles user notifications
 * 
 * 4. Resource Management:
 *    - Tracks active sessions
 *    - Manages username uniqueness
 *    - Handles cleanup on disconnection
 */
class ChatRoomActor : public qb::Actor {
private:
    std::map<qb::uuid, SessionInfo> _sessions;   // Maps session_id to SessionInfo
    std::map<std::string, qb::uuid> _usernames;  // Maps username to session_id for quick duplicate checking

public:
    /**
     * @brief Default constructor
     * 
     * QB actors should have minimal construction logic.
     * Main initialization happens in onInit().
     */
    ChatRoomActor() = default;

    /**
     * @brief Initializes the chat room actor
     * 
     * QB initialization sequence:
     * 1. Registers event handlers for:
     *    - AuthEvent: User authentication
     *    - ChatEvent: Message broadcasting
     *    - DisconnectEvent: Session cleanup
     * 2. Sets up internal state
     * 3. Prepares for message handling
     * 
     * @return true if initialization successful
     */
    bool onInit() override;

    /**
     * @brief Handles user authentication requests
     * 
     * Demonstrates QB's event handling pattern:
     * 1. Event Processing:
     *    - Validates username availability
     *    - Updates session mappings
     *    - Sends response to user
     * 
     * 2. State Management:
     *    - Updates _sessions map
     *    - Updates _usernames map
     *    - Maintains consistency
     * 
     * 3. Response Routing:
     *    - Sends confirmation to user
     *    - Broadcasts join notification
     *    - Handles errors
     * 
     * @param evt Authentication event containing session info and requested username
     */
    void on(AuthEvent& evt);

    /**
     * @brief Handles chat messages from users
     * 
     * Demonstrates QB's message broadcasting:
     * 1. Message Validation:
     *    - Verifies sender exists
     *    - Checks session validity
     * 
     * 2. Message Formatting:
     *    - Adds username prefix
     *    - Prepares for broadcast
     * 
     * 3. Distribution:
     *    - Broadcasts to all sessions
     *    - Routes through correct servers
     * 
     * @param evt Chat event containing the message and sender's session ID
     */
    void on(ChatEvent& evt);

    /**
     * @brief Handles user disconnections
     * 
     * Demonstrates QB's cleanup patterns:
     * 1. State Cleanup:
     *    - Removes from _sessions
     *    - Removes from _usernames
     *    - Maintains consistency
     * 
     * 2. Notification:
     *    - Broadcasts departure
     *    - Updates other users
     * 
     * 3. Resource Management:
     *    - Cleans up session data
     *    - Frees username
     * 
     * @param evt Disconnect event containing the session ID
     */
    void on(DisconnectEvent& evt);

private:
    /**
     * @brief Sends a message to a specific session
     * 
     * Demonstrates QB's targeted message delivery:
     * 1. Creates SendMessageEvent
     * 2. Routes to correct ServerActor
     * 3. Handles delivery
     * 
     * @param session_id Target session ID
     * @param server_id Server managing the target session
     * @param msg Message to send
     */
    void sendToSession(qb::uuid session_id, qb::ActorId server_id, const chat::Message& msg);

    /**
     * @brief Sends an error message to a specific session
     * 
     * Demonstrates QB's error handling pattern:
     * 1. Creates error message
     * 2. Sets appropriate type
     * 3. Routes to user
     * 
     * @param session_id Target session ID
     * @param server_id Server managing the target session
     * @param error Error message to send
     */
    void sendError(qb::uuid session_id, qb::ActorId server_id, const std::string& error);

    /**
     * @brief Broadcasts a message to all connected sessions
     * 
     * Demonstrates QB's broadcast pattern:
     * 1. Message Preparation:
     *    - Creates chat message
     *    - Sets broadcast type
     * 
     * 2. Distribution:
     *    - Iterates through sessions
     *    - Routes to correct servers
     *    - Handles all connected users
     * 
     * @param content Message content to broadcast
     */
    void broadcastMessage(const std::string& content);
}; 