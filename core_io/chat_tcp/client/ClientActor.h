/**
 * @file ClientActor.h
 * @brief Chat client actor managing network communication
 * 
 * This actor serves as the core of the chat client, providing:
 * - Robust connection management with automatic reconnection
 * - Protocol-compliant message handling
 * - State management and error recovery
 * - Integration with QB's actor framework
 */
#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/uri.h>
#include "../shared/Protocol.h"
#include "../shared/Events.h"
#include <atomic>

/**
 * @brief Core client actor managing network communication and message handling
 * 
 * Architecture:
 * - Inherits from QB's Actor system for event handling
 * - Uses QB's TCP client capabilities for network I/O
 * - Implements automatic reconnection with exponential backoff
 * - Maintains connection and authentication state
 * 
 * Responsibilities:
 * - Manages TCP connection to chat server
 * - Handles authentication flow
 * - Processes incoming messages
 * - Routes outgoing messages
 * - Manages connection lifecycle
 */
class ClientActor : public qb::Actor,
                   public qb::io::use<ClientActor>::tcp::client<> {
public:
    using Protocol = chat::ChatProtocol<ClientActor>;  ///< Chat protocol implementation

private:
    const std::string _username;           ///< Client's authentication username
    const qb::ActorId _input_actor;       ///< Reference to UI input handler
    const qb::io::uri _server_uri;        ///< Target server address
    
    std::atomic<bool> _connected{false};     ///< Current connection state
    std::atomic<bool> _authenticated{false}; ///< Current authentication state
    std::atomic<bool> _should_reconnect{true}; ///< Controls reconnection behavior
    
    /// Maximum time to wait for connection establishment
    static constexpr double CONNECT_TIMEOUT = 5.0;
    /// Delay between reconnection attempts
    static constexpr double RECONNECT_DELAY = 5.0;

public:
    /**
     * @brief Constructs a new client actor
     * 
     * Sets up the client actor with necessary configuration for:
     * - User identification
     * - Input handling
     * - Server connection
     * 
     * @param username Client's username for authentication
     * @param input_actor Reference to the input handling actor
     * @param server_uri Network address of the chat server
     */
    ClientActor(std::string username, qb::ActorId input_actor, qb::io::uri server_uri);
    
    /**
     * @brief Initializes the actor and begins connection sequence
     * 
     * Initialization steps:
     * 1. Registers event handlers
     * 2. Initiates server connection
     * 3. Sets up message processing
     * 
     * @return true if initialization succeeds
     */
    bool onInit() override;

    /**
     * @brief Processes incoming protocol messages
     * 
     * Handles various message types:
     * - AUTH_RESPONSE: Authentication results
     * - CHAT_MESSAGE: Chat content from other users
     * - ERROR: Server-side error notifications
     * 
     * @param msg The received protocol message
     */
    void on(const chat::Message& msg);

    /**
     * @brief Manages connection loss events
     * 
     * Recovery process:
     * 1. Updates connection state
     * 2. Clears authentication
     * 3. Initiates reconnection if enabled
     */
    void on(qb::io::async::event::disconnected const&);

    /**
     * @brief Processes user input events
     * 
     * Converts user input into protocol messages and:
     * - Validates connection state
     * - Ensures authentication
     * - Handles delivery failures
     * 
     * @param evt The input event from InputActor
     */
    void on(const ChatInputEvent& evt);

    /**
     * @brief Sends a chat message to the server
     * 
     * Message handling:
     * 1. Validates connection state
     * 2. Checks authentication
     * 3. Formats protocol message
     * 4. Handles delivery failures
     * 
     * @param message Content to send
     */
    void sendChat(const std::string& message);

    /**
     * @brief Performs clean disconnection
     * 
     * Shutdown sequence:
     * 1. Disables reconnection
     * 2. Closes active connection
     * 3. Updates internal state
     */
    void disconnect();

    /**
     * @brief Checks current connection state
     * @return true if connected to server
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Checks current authentication state
     * @return true if authenticated with server
     */
    bool isAuthenticated() const { return _authenticated; }

private:
    /**
     * @brief Manages server connection process
     * 
     * Connection flow:
     * 1. Initiates async connection
     * 2. Sets connection timeout
     * 3. Handles connection results
     */
    void connect();

    /**
     * @brief Handles successful connection establishment
     * 
     * Setup sequence:
     * 1. Configures transport
     * 2. Initializes protocol
     * 3. Starts authentication
     * 
     * @param socket Connected socket from QB framework
     */
    void onConnected(qb::io::tcp::socket&& socket);

    /**
     * @brief Manages connection failure recovery
     * 
     * Recovery steps:
     * 1. Updates connection state
     * 2. Notifies user
     * 3. Schedules reconnection if enabled
     */
    void onConnectionFailed();

    /**
     * @brief Manages reconnection scheduling
     * 
     * Uses QB's async callback system to:
     * 1. Implement reconnection delay
     * 2. Prevent tight retry loops
     * 3. Handle backoff timing
     */
    void scheduleReconnect();

    /**
     * @brief Initiates authentication sequence
     * 
     * Authentication flow:
     * 1. Creates AUTH_REQUEST message
     * 2. Includes username
     * 3. Sends to server
     */
    void authenticate();
}; 