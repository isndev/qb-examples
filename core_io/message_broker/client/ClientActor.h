/**
 * @file ClientActor.h
 * @brief Message broker client actor managing network communication
 * 
 * This actor serves as the core of the broker client, providing:
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
 * - Maintains connection state
 * 
 * Responsibilities:
 * - Manages TCP connection to broker server
 * - Handles message parsing and generation
 * - Processes incoming messages
 * - Routes outgoing messages
 * - Manages connection lifecycle
 */
class ClientActor : public qb::Actor,
                   public qb::io::use<ClientActor>::tcp::client<> {
public:
    using Protocol = broker::BrokerProtocol<ClientActor>;  ///< Broker protocol implementation

private:
    const qb::ActorId _input_actor;       ///< Reference to UI input handler
    const qb::io::uri _server_uri;        ///< Target server address
    
    std::atomic<bool> _connected{false};    ///< Current connection state
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
     * - Input handling
     * - Server connection
     * 
     * @param input_actor Reference to the input handling actor
     * @param server_uri Network address of the broker server
     */
    ClientActor(qb::ActorId input_actor, qb::io::uri server_uri);
    
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
     * - RESPONSE: Command responses
     * - MESSAGE: Published messages from subscribed topics
     * - ERROR: Server-side error notifications
     * 
     * @param msg The received protocol message
     */
    void on(const broker::Message& msg);

    /**
     * @brief Manages connection loss events
     * 
     * Recovery process:
     * 1. Updates connection state
     * 2. Initiates reconnection if enabled
     */
    void on(qb::io::async::event::disconnected const&);

    /**
     * @brief Processes user input events
     * 
     * Parses command string and:
     * - Validates connection state
     * - Creates appropriate protocol message
     * - Handles delivery failures
     * 
     * @param evt The input event from InputActor
     */
    void on(const BrokerInputEvent& evt);

    /**
     * @brief Sends a subscribe message to the server
     * 
     * Message handling:
     * 1. Validates connection state
     * 2. Formats protocol message
     * 3. Handles delivery failures
     * 
     * @param topic Topic to subscribe to
     */
    void sendSubscribe(const std::string& topic);

    /**
     * @brief Sends an unsubscribe message to the server
     * 
     * Message handling:
     * 1. Validates connection state
     * 2. Formats protocol message
     * 3. Handles delivery failures
     * 
     * @param topic Topic to unsubscribe from
     */
    void sendUnsubscribe(const std::string& topic);

    /**
     * @brief Sends a publish message to the server
     * 
     * Message handling:
     * 1. Validates connection state
     * 2. Formats protocol message
     * 3. Handles delivery failures
     * 
     * @param topic Topic to publish to
     * @param message Message content
     */
    void sendPublish(const std::string& topic, const std::string& message);

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
     * 3. Updates connection state
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
     * @brief Parses user commands
     * 
     * Command parsing:
     * 1. Identifies command type (SUB, UNSUB, PUB)
     * 2. Extracts parameters
     * 3. Routes to appropriate handlers
     * 
     * @param command User input command string
     */
    void processCommand(const std::string& command);
}; 