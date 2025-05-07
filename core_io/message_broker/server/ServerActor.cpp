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
#include "BrokerSession.h"
#include "../shared/Events.h"
#include <iostream>

/**
 * Constructor showing QB actor initialization:
 * - Minimal construction pattern
 * - Stores dependencies for later use
 * - Prepares for session management
 */
ServerActor::ServerActor(qb::ActorId topic_manager_id)
    : _topic_manager_id(topic_manager_id) {}

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
    qb::io::cout() << "ServerActor initialized with ID: " << id() << std::endl;
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
    // Create and register a new broker session for the incoming connection
    auto& session = registerSession(std::move(evt.socket));
    qb::io::cout() << "New broker session registered: " << session.id() << std::endl;
}

/**
 * Subscription handler showing QB's event routing:
 * 1. Event Creation:
 *    - Creates SubscribeEvent for TopicManager with zero-copy message data
 *    - Sets session and topic
 *    - Routes to correct actor
 * 
 * 2. State Management:
 *    - Maintains session mapping
 *    - Tracks subscription state
 *    - Enables topic management
 */
void ServerActor::handleSubscribe(qb::uuid session_id, broker::Message&& msg) {
    // Forward subscription request to TopicManagerActor with optimized message handling
    // The SubscribeEvent constructor handles the message ownership and string_view creation
    auto& evt = push<SubscribeEvent>(_topic_manager_id, session_id, std::move(msg));
    
    qb::io::cout() << "Forwarding subscription request for topic: " << evt.topic
              << " from session: " << session_id << std::endl;
}

/**
 * Unsubscription handler showing QB's event routing:
 * 1. Event Creation:
 *    - Creates UnsubscribeEvent for TopicManager with zero-copy message data
 *    - Sets session and topic
 *    - Routes to correct actor
 * 
 * 2. State Management:
 *    - Maintains consistency with subscription state
 *    - Tracks topic membership
 *    - Enables proper cleanup
 */
void ServerActor::handleUnsubscribe(qb::uuid session_id, broker::Message&& msg) {
    // Forward unsubscription request to TopicManagerActor with optimized message handling
    // The UnsubscribeEvent constructor handles the message ownership and string_view creation
    auto& evt = push<UnsubscribeEvent>(_topic_manager_id, session_id, std::move(msg));
    
    qb::io::cout() << "Forwarding unsubscription request for topic: " << evt.topic
              << " from session: " << session_id << std::endl;
}

/**
 * Publish handler showing QB's message routing:
 * 1. Message Processing:
 *    - Creates PublishEvent with zero-copy references
 *    - Sets message content
 *    - Routes to TopicManager
 * 
 * 2. Session Validation:
 *    - Verifies session exists
 *    - Ensures proper routing
 *    - Maintains message flow
 */
void ServerActor::handlePublish(qb::uuid session_id, broker::MessageContainer&& container, 
                                std::string_view topic, std::string_view content) {
    // Forward publish message to TopicManagerActor for broadcasting
    // Using the PublishEvent constructor that takes a MessageContainer
    auto& evt = push<PublishEvent>(_topic_manager_id, session_id, std::move(container), topic, content);
    
    qb::io::cout() << "Forwarding publish request to topic: " << evt.topic
              << " from session: " << session_id << std::endl;
}

/**
 * Disconnection handler showing QB's cleanup patterns:
 * 1. Session Cleanup:
 *    - Notifies TopicManager
 *    - Removes session state
 *    - Updates subscription tracking
 * 
 * 2. Resource Management:
 *    - Cleans up session resources
 *    - Updates system state
 *    - Maintains consistency
 */
void ServerActor::handleDisconnect(qb::uuid session_id) {
    // Notify TopicManagerActor about client disconnection
    auto& evt = push<DisconnectEvent>(_topic_manager_id);
    evt.session_id = session_id;
    
    qb::io::cout() << "Notifying topic manager about disconnected session: " << session_id << std::endl;
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
        // Use the message accessor to get the properly owned message for sending
        *it->second << evt.message(); // Send message to session
        it->second->updateTimeout(); // Update session timeout to prevent disconnection
    }
} 