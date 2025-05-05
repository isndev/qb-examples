/**
 * @file ClientActor.cpp
 * @brief Implementation of the chat client's network actor
 * 
 * This file implements the core client functionality including:
 * - Connection management and recovery
 * - Message processing and routing
 * - State management and transitions
 * - Error handling and user feedback
 */

#include "ClientActor.h"
#include <iostream>

/**
 * @brief Constructs client actor with required configuration
 * 
 * Initializes the actor with immutable configuration:
 * - Username for authentication
 * - Input actor for UI integration
 * - Server URI for connection
 */
ClientActor::ClientActor(std::string username, qb::ActorId input_actor, qb::io::uri server_uri)
    : _username(std::move(username))
    , _input_actor(input_actor)
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
    std::cout << "ClientActor initialized with ID: " << id() << std::endl;
    registerEvent<ChatInputEvent>(*this);
    connect();
    return true;
}

/**
 * @brief Processes incoming server messages
 * 
 * Message handling by type:
 * - AUTH_RESPONSE: Updates authentication state
 * - CHAT_MESSAGE: Displays chat content
 * - ERROR: Shows error notifications
 * 
 * All messages are displayed to the user with appropriate formatting.
 * 
 * @param msg The received protocol message
 */
void ClientActor::on(const chat::Message& msg) {
    switch(msg.type) {
        case chat::MessageType::AUTH_RESPONSE:
            std::cout << "Server: " << msg.payload << std::endl;
            _authenticated = true;
            break;
            
        case chat::MessageType::CHAT_MESSAGE:
            std::cout << msg.payload << std::endl;
            break;
            
        case chat::MessageType::ERROR:
            std::cerr << "Error: " << msg.payload << std::endl;
            break;
            
        default:
            std::cerr << "Unknown message type: " << static_cast<int>(msg.type) << std::endl;
            break;
    }
}

/**
 * @brief Handles connection loss events
 * 
 * Recovery process:
 * 1. Updates connection state
 * 2. Resets authentication
 * 3. Notifies user
 * 4. Initiates reconnection if enabled
 * 
 * Uses QB's async callback system for reconnection timing.
 */
void ClientActor::on(qb::io::async::event::disconnected const&) {
    std::cout << "Disconnected from server" << std::endl;
    _connected = false;
    _authenticated = false;
    
    if (_should_reconnect) {
        // Schedule async reconnection with delay
        qb::io::async::callback([this]() {
            std::cout << "Attempting to reconnect..." << std::endl;
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
 * 4. Starts authentication
 * 
 * @param socket The connected socket from QB framework
 */
void ClientActor::onConnected(qb::io::tcp::socket&& socket) {
    std::cout << "Connected to server" << std::endl;
    _connected = true;
    
    // Configure the connection
    this->transport().close();
    this->in().reset();
    this->out().reset();
    this->transport() = std::move(socket);
    this->template switch_protocol<Protocol>(*this);
    this->start();
    
    // Begin authentication sequence
    authenticate();
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
    std::cout << "Connection failed" << std::endl;
    if (_should_reconnect) {
        // Schedule delayed reconnection
        qb::io::async::callback([this]() {
            std::cout << "Retrying connection..." << std::endl;
            connect();
        }, RECONNECT_DELAY);
    }
}

/**
 * @brief Sends authentication request
 * 
 * Creates and sends AUTH_REQUEST message with:
 * - Client username
 * - Protocol formatting
 * - QB's message routing
 */
void ClientActor::authenticate() {
    chat::Message auth;
    auth.type = chat::MessageType::AUTH_REQUEST;
    auth.payload = _username;
    *this << auth;
}

/**
 * @brief Sends chat message to server
 * 
 * Message processing:
 * 1. Validates connection state
 * 2. Checks authentication
 * 3. Creates protocol message
 * 4. Routes to server
 * 
 * @param message Content to send
 */
void ClientActor::sendChat(const std::string& message) {
    if (!_connected) {
        std::cout << "Not connected to server. Message discarded." << std::endl;
        return;
    }
    
    if (!_authenticated) {
        std::cout << "Not authenticated. Message discarded." << std::endl;
        return;
    }
    
    chat::Message msg;
    msg.type = chat::MessageType::CHAT_MESSAGE;
    msg.payload = message;
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
 * Converts ChatInputEvent to protocol message and:
 * 1. Validates states
 * 2. Formats message
 * 3. Sends to server
 * 
 * @param evt The input event from InputActor
 */
void ClientActor::on(const ChatInputEvent& evt) {
    sendChat(evt.message);
} 