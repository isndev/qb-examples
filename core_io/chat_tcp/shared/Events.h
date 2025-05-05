#pragma once

#include <qb/event.h>
#include <qb/io/tcp/socket.h>
#include <qb/string.h>
#include "Protocol.h"

/**
 * @file Events.h
 * @brief Event definitions for the QB chat system
 * 
 * This file defines the event types that enable communication between
 * different actors in the chat system. The events form the backbone of
 * the system's message passing architecture, enabling:
 * 
 * - Asynchronous communication between actors
 * - Type-safe message passing
 * - Clear separation of concerns
 * - Efficient state management
 */

/**
 * @brief Event triggered when a new TCP connection is accepted
 * 
 * Flow:
 * 1. AcceptActor receives new TCP connection
 * 2. Creates NewSessionEvent with connected socket
 * 3. Routes event to ServerActor for session creation
 * 
 * This event initiates the client connection lifecycle and enables
 * proper resource management through QB's RAII patterns.
 */
struct NewSessionEvent : public qb::Event {
    qb::io::tcp::socket socket;  ///< Connected client socket with RAII management
};

/**
 * @brief Event for user authentication requests
 * 
 * Flow:
 * 1. Client sends AUTH_REQUEST message
 * 2. ServerActor creates AuthEvent
 * 3. ChatRoomActor processes authentication:
 *    - Validates username availability
 *    - Registers user if allowed
 *    - Sends AUTH_RESPONSE
 * 
 * The event source (evt.getSource()) contains the ServerActor's ID,
 * enabling ChatRoomActor to route responses back to the correct client.
 */
struct AuthEvent : public qb::Event {
    qb::uuid session_id;        ///< Unique session identifier
    qb::string<32> username;    ///< Requested username (max 32 chars)
};

/**
 * @brief Event for chat message distribution
 * 
 * Flow:
 * 1. Client sends CHAT_MESSAGE
 * 2. ServerActor creates ChatEvent
 * 3. ChatRoomActor processes message:
 *    - Looks up sender's username
 *    - Formats message with sender info
 *    - Broadcasts to all connected clients
 * 
 * Messages are limited to 256 characters to prevent abuse
 * and ensure efficient processing.
 */
struct ChatEvent : public qb::Event {
    qb::uuid session_id;          ///< Message sender's session ID
    qb::string<256> message;      ///< Message content (max 256 chars)
};

/**
 * @brief Event for targeted message delivery
 * 
 * Flow:
 * 1. ChatRoomActor creates SendMessageEvent
 * 2. Routes to specific ServerActor
 * 3. ServerActor sends through client's socket
 * 
 * This event enables direct message delivery to specific
 * clients while maintaining actor system boundaries.
 */
struct SendMessageEvent : public qb::Event {
    qb::uuid session_id;     ///< Target client's session ID
    chat::Message message;   ///< Protocol message to deliver
};

/**
 * @brief Event for client disconnection handling
 * 
 * Flow:
 * 1. Client disconnects or timeout occurs
 * 2. ServerActor detects disconnection
 * 3. ChatRoomActor processes cleanup:
 *    - Removes from session tracking
 *    - Frees username
 *    - Notifies other clients
 * 
 * The event source (evt.getSource()) identifies the ServerActor
 * that was handling the disconnected client.
 */
struct DisconnectEvent : public qb::Event {
    qb::uuid session_id;     ///< ID of the disconnected session
};

/**
 * @brief Event for client-side user input handling
 * 
 * Flow:
 * 1. User enters message in client
 * 2. InputActor creates ChatInputEvent
 * 3. ClientActor processes input:
 *    - Formats as protocol message
 *    - Sends to server if connected
 * 
 * This event separates input handling from network I/O,
 * enabling clean separation of concerns in the client.
 */
struct ChatInputEvent : public qb::Event {
    qb::string<256> message;    ///< User input message (max 256 chars)
}; 