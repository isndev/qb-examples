/**
 * @file BrokerSession.cpp
 * @brief Implementation demonstrating QB's session management patterns
 * 
 * This file demonstrates:
 * 1. How to implement QB session lifecycle management
 * 2. How to handle protocol messages in a session
 * 3. How to integrate timeout mechanisms
 * 4. How to manage client disconnections
 */

#include "BrokerSession.h"
#include "ServerActor.h"
#include <iostream>

/**
 * Session constructor showing QB initialization sequence:
 * 1. Base constructor sets up TCP client functionality
 * 2. Protocol switching configures message handling
 * 3. Timeout setup ensures resource cleanup
 * 4. Logging helps with debugging and monitoring
 */
BrokerSession::BrokerSession(ServerActor& server)
    : client(server) {
    // Set up protocol handler and timeout
    this->template switch_protocol<Protocol>(*this);
    this->setTimeout(600);  // 120 second timeout
    std::cout << "New broker client connected" << std::endl;
}

/**
 * Destructor demonstrating QB cleanup pattern:
 * - Called on normal disconnection
 * - Called on timeout
 * - Called on server shutdown
 * - Ensures proper resource cleanup
 */
BrokerSession::~BrokerSession() {
    std::cout << "Broker client disconnected" << std::endl;
}

/**
 * Message handler showing QB's protocol integration with zero-copy optimization:
 * 1. Protocol Framework:
 *    - Automatically parses incoming data
 *    - Validates message format
 *    - Routes to appropriate handler
 * 
 * 2. Message Routing with Zero-Copy:
 *    - SUBSCRIBE: Forward moved message to subscription handler
 *    - UNSUBSCRIBE: Forward moved message to unsubscription handler
 *    - PUBLISH: Forward moved message with parsed string_views
 *    - Handles unknown message types
 * 
 * 3. Server Integration:
 *    - Uses server() to access parent
 *    - Maintains proper actor hierarchy
 *    - Routes messages to correct handlers
 */
void BrokerSession::on(broker::Message msg) {
    // Route incoming messages to appropriate handlers in ServerActor
    switch(msg.type) {
        case broker::MessageType::SUBSCRIBE:
            // Move the message directly to avoid any copies
            this->server().handleSubscribe(this->id(), std::move(msg));
            break;
            
        case broker::MessageType::UNSUBSCRIBE:
            // Move the message directly to avoid any copies
            this->server().handleUnsubscribe(this->id(), std::move(msg));
            break;
            
        case broker::MessageType::PUBLISH: {
            // Parse the publish message (format: "topic message")
            std::string_view payload_view = msg.payload;
            size_t space_pos = payload_view.find(' ');
            
            if (space_pos != std::string_view::npos) {
                // 1. Create a MessageContainer that takes ownership of the message
                broker::MessageContainer container(std::move(msg));
                
                // 2. Create string_views from the container (guarantees data exists)
                std::string_view container_payload = container.payload();
                std::string_view topic = container_payload.substr(0, space_pos);
                std::string_view content = container_payload.substr(space_pos + 1);
                
                // 3. Pass the container and views to the server
                this->server().handlePublish(
                    this->id(), 
                    std::move(container),
                    topic, 
                    content
                );
            } else {
                // Send error if format is incorrect
                broker::Message error_msg;
                error_msg.type = broker::MessageType::ERROR;
                error_msg.payload = "Invalid publish format. Use: PUB <topic> <message>";
                *this << error_msg;
            }
            break;
        }
        default:
            std::cerr << "Unknown message type: " << static_cast<int>(msg.type) << std::endl;
            break;
    }
    this->updateTimeout(); // Update timeout to prevent disconnection
}

/**
 * Disconnection handler showing QB's event system:
 * 1. Event Handling:
 *    - Automatic detection of disconnection
 *    - Clean notification system
 *    - Resource management
 * 
 * 2. Server Notification:
 *    - Informs server of disconnection
 *    - Allows for client cleanup
 *    - Maintains system consistency
 */
void BrokerSession::on(qb::io::async::event::disconnected const &) {
    // Notify server when client disconnects
    std::cout << "Broker client disconnected" << std::endl;
    this->server().handleDisconnect(this->id());
}

/**
 * Timeout handler demonstrating QB's timeout management:
 * 1. Automatic Detection:
 *    - QB tracks session activity
 *    - Triggers on inactivity
 *    - Configurable duration
 * 
 * 2. Resource Protection:
 *    - Prevents resource leaks
 *    - Cleans up inactive sessions
 *    - Maintains system stability
 */
void BrokerSession::on(qb::io::async::event::timer const &) {
    // Handle session timeout by disconnecting the client
    std::cout << "Broker client timed out" << std::endl;
    this->disconnect();
} 