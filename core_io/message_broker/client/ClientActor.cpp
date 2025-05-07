/**
 * @file ClientActor.cpp
 * @brief Implementation of the broker client's network actor
 * 
 * This file implements the core client functionality including:
 * - Connection management and recovery
 * - Message processing and routing
 * - State management and transitions
 * - Error handling and user feedback
 */

#include "ClientActor.h"
#include <iostream>
#include <sstream>

/**
 * @brief Constructs client actor with required configuration
 * 
 * Initializes the actor with immutable configuration:
 * - Input actor for UI integration
 * - Server URI for connection
 */
ClientActor::ClientActor(qb::ActorId input_actor, qb::io::uri server_uri)
    : _input_actor(input_actor)
    , _server_uri(std::move(server_uri)) {}

/**
 * @brief Initializes the client actor system
 * 
 * Setup sequence:
 * 1. Registers input event handler
 * 2. Initiates server connection
 * 3. Prepares message processing
 */
bool ClientActor::onInit() {
    qb::io::cout() << "ClientActor initialized with ID: " << id() << std::endl;
    registerEvent<BrokerInputEvent>(*this);
    connect();
    return true;
}

/**
 * @brief Processes incoming server messages
 * 
 * Message handling by type:
 * - RESPONSE: Command responses
 * - MESSAGE: Topic messages from subscriptions
 * - ERROR: Error notifications
 * 
 * All messages are displayed to the user with appropriate formatting.
 * 
 * @param msg The received protocol message
 */
void ClientActor::on(const broker::Message& msg) {
    switch(msg.type) {
        case broker::MessageType::RESPONSE:
            qb::io::cout() << "Server: " << msg.payload << std::endl;
            break;
            
        case broker::MessageType::MESSAGE:
            qb::io::cout() << "Message: " << msg.payload << std::endl;
            break;
            
        case broker::MessageType::ERROR:
            qb::io::cerr() << "Error: " << msg.payload << std::endl;
            break;
            
        default:
            qb::io::cerr() << "Unknown message type: " << static_cast<int>(msg.type) << std::endl;
            break;
    }
}

/**
 * @brief Handles connection loss events
 * 
 * Recovery process:
 * 1. Updates connection state
 * 2. Notifies user
 * 3. Initiates reconnection if enabled
 * 
 * Uses QB's async callback system for reconnection timing.
 */
void ClientActor::on(qb::io::async::event::disconnected const&) {
    qb::io::cout() << "Disconnected from server" << std::endl;
    _connected = false;
    
    if (_should_reconnect) {
        // Schedule async reconnection with delay
        qb::io::async::callback([this]() {
            qb::io::cout() << "Attempting to reconnect..." << std::endl;
            connect();
        }, RECONNECT_DELAY);
    }
}

/**
 * @brief Initiates server connection
 * 
 * Connection process:
 * 1. Creates async connection request
 * 2. Sets connection timeout
 * 3. Routes to success/failure handlers
 * 
 * Uses QB's async TCP connect system for non-blocking operation.
 */
void ClientActor::connect() {
    qb::io::async::tcp::connect<qb::io::tcp::socket>(
        _server_uri,
        [this](qb::io::tcp::socket socket) {
            if (socket.is_open()) {
                onConnected(std::move(socket));
            } else {
                onConnectionFailed();
            }
        },
        CONNECT_TIMEOUT
    );
}

/**
 * @brief Sets up successful connection
 * 
 * Setup sequence:
 * 1. Updates connection state
 * 2. Configures transport
 * 3. Initializes protocol
 * 
 * @param socket The connected socket from QB framework
 */
void ClientActor::onConnected(qb::io::tcp::socket&& socket) {
    qb::io::cout() << "Connected to server" << std::endl;
    _connected = true;
    
    // Configure the connection
    this->transport().close();
    this->in().reset();
    this->out().reset();
    this->transport() = std::move(socket);
    this->template switch_protocol<Protocol>(*this);
    this->start();
}

/**
 * @brief Handles connection failures
 * 
 * Recovery process:
 * 1. Notifies user
 * 2. Schedules reconnection if enabled
 * 
 * Uses QB's async callback for reconnection timing.
 */
void ClientActor::onConnectionFailed() {
    qb::io::cout() << "Connection failed" << std::endl;
    if (_should_reconnect) {
        // Schedule delayed reconnection
        qb::io::async::callback([this]() {
            qb::io::cout() << "Retrying connection..." << std::endl;
            connect();
        }, RECONNECT_DELAY);
    }
}

/**
 * @brief Sends a subscribe message to the server
 * 
 * Message processing:
 * 1. Validates connection state
 * 2. Creates protocol message
 * 3. Routes to server
 * 
 * @param topic Topic to subscribe to
 */
void ClientActor::sendSubscribe(const std::string& topic) {
    if (!_connected) {
        qb::io::cout() << "Not connected to server. Command discarded." << std::endl;
        return;
    }
    
    broker::Message msg;
    msg.type = broker::MessageType::SUBSCRIBE;
    msg.payload = topic;
    *this << msg;
}

/**
 * @brief Sends an unsubscribe message to the server
 * 
 * Message processing:
 * 1. Validates connection state
 * 2. Creates protocol message
 * 3. Routes to server
 * 
 * @param topic Topic to unsubscribe from
 */
void ClientActor::sendUnsubscribe(const std::string& topic) {
    if (!_connected) {
        qb::io::cout() << "Not connected to server. Command discarded." << std::endl;
        return;
    }
    
    broker::Message msg;
    msg.type = broker::MessageType::UNSUBSCRIBE;
    msg.payload = topic;
    *this << msg;
}

/**
 * @brief Sends a publish message to the server
 * 
 * Message processing:
 * 1. Validates connection state
 * 2. Creates protocol message
 * 3. Routes to server
 * 
 * @param topic Topic to publish to
 * @param message Message content
 */
void ClientActor::sendPublish(const std::string& topic, const std::string& message) {
    if (!_connected) {
        qb::io::cout() << "Not connected to server. Command discarded." << std::endl;
        return;
    }
    
    broker::Message msg;
    msg.type = broker::MessageType::PUBLISH;
    msg.payload = topic + " " + message;
    *this << msg;
}

/**
 * @brief Initiates clean disconnection
 * 
 * Shutdown sequence:
 * 1. Disables reconnection
 * 2. Closes active connection
 * 3. State cleanup handled by disconnect event
 */
void ClientActor::disconnect() {
    _should_reconnect = false;
    if (_connected) {
        this->transport().close();
    }
}

/**
 * @brief Processes user input events
 * 
 * Command parsing:
 * 1. Extracts command type
 * 2. Routes to appropriate handler
 * 3. Provides feedback
 * 
 * @param evt The input event from InputActor
 */
void ClientActor::on(const BrokerInputEvent& evt) {
    processCommand(evt.command);
}

/**
 * @brief Parses and handles user commands
 * 
 * Command format:
 * - SUB <topic> - Subscribe to a topic
 * - UNSUB <topic> - Unsubscribe from a topic
 * - PUB <topic> <message> - Publish to a topic
 * 
 * @param command The command string to process
 */
void ClientActor::processCommand(const std::string& command) {
    std::istringstream iss(command);
    std::string cmd, topic, message_part;
    
    // Read command type (SUB, UNSUB, PUB)
    if (!(iss >> cmd)) {
        qb::io::cerr() << "Invalid command format" << std::endl;
        return;
    }
    
    // Convert to uppercase for case-insensitive comparison
    for (auto& c : cmd) c = toupper(c);
    
    // Handle command based on type
    if (cmd == "SUB" || cmd == "SUBSCRIBE") {
        if (!(iss >> topic)) {
            qb::io::cerr() << "Missing topic. Format: SUB <topic>" << std::endl;
            return;
        }
        sendSubscribe(topic);
    }
    else if (cmd == "UNSUB" || cmd == "UNSUBSCRIBE") {
        if (!(iss >> topic)) {
            qb::io::cerr() << "Missing topic. Format: UNSUB <topic>" << std::endl;
            return;
        }
        sendUnsubscribe(topic);
    }
    else if (cmd == "PUB" || cmd == "PUBLISH") {
        if (!(iss >> topic)) {
            qb::io::cerr() << "Missing topic. Format: PUB <topic> <message>" << std::endl;
            return;
        }
        
        // Read the rest of the line as message
        std::string message;
        std::getline(iss >> std::ws, message);
        
        if (message.empty()) {
            qb::io::cerr() << "Missing message. Format: PUB <topic> <message>" << std::endl;
            return;
        }
        
        sendPublish(topic, message);
    }
    else {
        qb::io::cerr() << "Unknown command. Valid commands: SUB, UNSUB, PUB" << std::endl;
    }
} 