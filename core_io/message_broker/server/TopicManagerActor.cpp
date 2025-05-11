/**
 * @file examples/core_io/message_broker/server/TopicManagerActor.cpp
 * @example Message Broker Server - Topic Management Implementation
 * @brief Implements `TopicManagerActor` for managing topics, subscriptions, and
 *        message broadcasting in the message broker.
 *
 * @details
 * This file provides the `TopicManagerActor` implementation.
 * - `onInit()`: Registers handlers for `SubscribeEvent`, `UnsubscribeEvent`, `PublishEvent`, and `DisconnectEvent`.
 * - `on(SubscribeEvent&)`: Adds the client session (`evt.session_id`) to the subscriber list for
 *   `evt.topic` (a `std::string_view` into `evt.message_data`). Updates internal maps and sends a response.
 * - `on(UnsubscribeEvent&)`: Removes the client session from the topic's subscriber list. Cleans up
 *   empty topics and sends a response.
 * - `on(PublishEvent&)`: When a message is published to `evt.topic` (a `std::string_view`):
 *   - It formats the message for delivery.
 *   - It creates a single `broker::MessageContainer shared_message` containing this formatted payload.
 *   - It then iterates through all subscribers for that topic.
 *   - For each subscriber, it creates a `SendMessageEvent` that references this *same* `shared_message`
 *     (via `SendMessageEvent`'s constructor that takes a const reference to `MessageContainer`).
 *   - This `SendMessageEvent` is `push`ed to the `ServerActor` responsible for that subscriber.
 *   This demonstrates efficient broadcasting by sharing message data via `MessageContainer`'s
 *   `std::shared_ptr` semantics, avoiding multiple copies of the payload.
 * - `on(DisconnectEvent&)`: Cleans up all subscriptions associated with the disconnected session ID.
 * - `sendToSession()` (two overloads): Helper methods to `push` a `SendMessageEvent` to the appropriate
 *   `ServerActor`. One overload takes message type and payload string, creating a new `MessageContainer`.
 *   The other takes a `const broker::MessageContainer&`, allowing shared message data to be passed.
 * - `sendError()`, `sendResponse()`: Utility methods using `sendToSession()`.
 *
 * QB Features Demonstrated (in context of this implementation):
 * - `qb::Actor` event handling and state management for core broker logic.
 * - Efficient broadcasting using `broker::MessageContainer` to share message data among multiple `SendMessageEvent`s.
 * - Use of `std::string_view` to refer to parts of message payloads without copying.
 * - Complex state management (`_sessions`, `_subscriptions`, `_session_topics`).
 * - `qb::io::cout`: Thread-safe console output.
 */

#include "TopicManagerActor.h"
#include <iostream>

/**
 * @brief Initializes the topic manager actor and registers event handlers
 * 
 * Sets up handlers for:
 * - Subscription events (client subscribe/unsubscribe)
 * - Publish events (message broadcasting)
 * - Disconnect events (client departure handling)
 * 
 * @return true if initialization succeeds, false otherwise
 */
bool TopicManagerActor::onInit() {
    registerEvent<SubscribeEvent>(*this);
    registerEvent<UnsubscribeEvent>(*this);
    registerEvent<PublishEvent>(*this);
    registerEvent<DisconnectEvent>(*this);
    qb::io::cout() << "TopicManagerActor initialized with ID: " << id() << std::endl;
    return true;
}

/**
 * @brief Handles topic subscription requests with zero-copy optimization
 * 
 * Subscription flow:
 * 1. Validates session exists or registers it
 * 2. Adds client to topic subscription list
 * 3. Updates client's topic list
 * 4. Sends confirmation message to the client
 * 
 * Uses string_view for efficient topic access without copying.
 * 
 * @param evt The subscription event containing session and topic details
 */
void TopicManagerActor::on(SubscribeEvent& evt) {
    auto session_id = evt.session_id;
    auto topic_view = evt.topic;
    auto server_id = evt.getSource();
    
    // Convert string_view to std::string only when needed for storage
    std::string topic_str(topic_view);

    // Register the session if not already registered
    if (_sessions.find(session_id) == _sessions.end()) {
        _sessions[session_id] = SessionInfo{server_id};
    }

    // Add session to topic subscribers
    _subscriptions[topic_str].insert(session_id);
    
    // Add topic to session's subscriptions
    _session_topics[session_id].insert(topic_str);

    // Send confirmation
    qb::io::cout() << "Client " << session_id << " subscribed to topic: " << topic_str << std::endl;
    sendResponse(session_id, server_id, "Subscribed to topic: " + topic_str);
}

/**
 * @brief Handles topic unsubscription requests with zero-copy optimization
 * 
 * Unsubscription flow:
 * 1. Validates session and topic exist
 * 2. Removes client from topic subscription list
 * 3. Updates client's topic list
 * 4. Sends confirmation message to the client
 * 
 * Uses string_view for efficient topic access without copying.
 * 
 * @param evt The unsubscription event containing session and topic details
 */
void TopicManagerActor::on(UnsubscribeEvent& evt) {
    auto session_id = evt.session_id;
    auto topic_view = evt.topic;
    auto server_id = evt.getSource();
    
    // Convert string_view to std::string only when needed for storage/lookup
    std::string topic_str(topic_view);

    // Verify session exists
    if (_sessions.find(session_id) == _sessions.end()) {
        sendError(session_id, server_id, "Session not registered");
        return;
    }

    // Check if topic exists and session is subscribed
    auto topic_it = _subscriptions.find(topic_str);
    if (topic_it == _subscriptions.end() || 
        topic_it->second.find(session_id) == topic_it->second.end()) {
        sendError(session_id, server_id, "Not subscribed to topic: " + topic_str);
        return;
    }

    // Remove session from topic subscribers
    topic_it->second.erase(session_id);
    
    // Remove topic from session's subscriptions
    _session_topics[session_id].erase(topic_str);
    
    // Clean up empty topics
    if (topic_it->second.empty()) {
        _subscriptions.erase(topic_it);
    }

    // Send confirmation
    qb::io::cout() << "Client " << session_id << " unsubscribed from topic: " << topic_str << std::endl;
    sendResponse(session_id, server_id, "Unsubscribed from topic: " + topic_str);
}

/**
 * @brief Processes and broadcasts messages to topic subscribers with zero-copy optimization
 * 
 * Message handling:
 * 1. Validates session exists
 * 2. Creates a single shared message container
 * 3. Broadcasts to all subscribers using atomic shared message data
 * 
 * Uses atomic sharing of the message data between all subscribers to avoid
 * any unnecessary copies, with thread-safe cleanup via shared_ptr.
 * 
 * @param evt The publish event containing the message
 */
void TopicManagerActor::on(PublishEvent& evt) {
    auto session_id = evt.session_id;
    auto topic_view = evt.topic;
    auto content_view = evt.content;
    auto server_id = evt.getSource();
    
    // Convert string_view to std::string only when needed for storage/lookup
    std::string topic_str(topic_view);

    // Check if topic has subscribers
    auto topic_it = _subscriptions.find(topic_str);
    if (topic_it == _subscriptions.end() || topic_it->second.empty()) {
        // No subscribers, just acknowledge
        sendResponse(session_id, server_id, "Message published to topic with no subscribers: " + topic_str);
        return;
    }

    // Format message for delivery - convert to string only for storage/broadcasting
    std::string formatted_message = topic_str + ": " + std::string(content_view);
    
    // Create a SINGLE shared message container that will be used by all subscribers
    // This is the key optimization - creating one shared message that will be
    // referenced by all subscriber events
    broker::MessageContainer shared_message(broker::MessageType::MESSAGE, formatted_message);
    
    // Broadcast to all subscribers
    qb::io::cout() << "Broadcasting message to topic " << topic_str << " with "
              << topic_it->second.size() << " subscribers" << std::endl;
    
    for (const auto& subscriber_id : topic_it->second) {
        // Broadcast to all subscribers, including the publisher if they're subscribed
        auto subscriber_it = _sessions.find(subscriber_id);
        if (subscriber_it != _sessions.end()) {
            // Send the message using the shared container
            // Each event will reference the same underlying message data
            sendToSession(subscriber_id, subscriber_it->second.server_id, shared_message);
        }
    }

    // No confirmation is sent to publisher - standard broker behavior
}

/**
 * @brief Manages client disconnection and cleanup
 * 
 * Disconnection flow:
 * 1. Removes client from all topic subscriptions
 * 2. Cleans up client's topic list
 * 3. Removes client from session tracking
 * 
 * @param evt The disconnect event for the session
 */
void TopicManagerActor::on(DisconnectEvent& evt) {
    auto session_id = evt.session_id;
    
    // Check if session exists
    auto session_it = _sessions.find(session_id);
    if (session_it == _sessions.end()) return;

    // Get all topics the session was subscribed to
    auto topics_it = _session_topics.find(session_id);
    if (topics_it != _session_topics.end()) {
        // Remove session from each topic's subscribers
        for (const auto& topic : topics_it->second) {
            auto& subscribers = _subscriptions[topic];
            subscribers.erase(session_id);
            
            // Remove topic if empty
            if (subscribers.empty()) {
                _subscriptions.erase(topic);
            }
        }
        
        // Clean up session's topics
        _session_topics.erase(topics_it);
    }
    
    // Remove session info
    _sessions.erase(session_it);
    
    qb::io::cout() << "Client " << session_id << " disconnected and removed from all topics" << std::endl;
}

/**
 * @brief Sends a message to a specific session
 * 
 * Routes messages through the appropriate server actor
 * using QB's event system with optimized MessageContainer.
 * 
 * @param session_id Target session's unique identifier
 * @param server_id Server actor handling the session
 * @param type The message type to send
 * @param payload The message payload
 */
void TopicManagerActor::sendToSession(qb::uuid session_id, qb::ActorId server_id, 
                                      broker::MessageType type, const std::string& payload) {
    // Create event with optimized message handling
    auto& evt = push<SendMessageEvent>(server_id, session_id, type, payload);
}

/**
 * @brief Sends a message to a specific session using a shared message container
 * 
 * This method optimizes broadcasting by sharing the same message data
 * between all recipients, eliminating unnecessary copies.
 * 
 * @param session_id Target session's unique identifier
 * @param server_id Server actor handling the session
 * @param shared_message A shared message container used across multiple recipients
 */
void TopicManagerActor::sendToSession(qb::uuid session_id, qb::ActorId server_id,
                                     const broker::MessageContainer& shared_message) {
    // Create event that references the shared message container
    // The underlying message data will be atomically shared
    auto& evt = push<SendMessageEvent>(server_id, session_id, shared_message);
}

/**
 * @brief Sends an error message to a specific session
 * 
 * Creates and routes an error message through the broker protocol.
 * 
 * @param session_id Target session's unique identifier
 * @param server_id Server actor handling the session
 * @param error The error message to send
 */
void TopicManagerActor::sendError(qb::uuid session_id, qb::ActorId server_id, const std::string& error) {
    sendToSession(session_id, server_id, broker::MessageType::ERROR, error);
}

/**
 * @brief Sends a response message to a specific session
 * 
 * Creates and routes a response message through the broker protocol.
 * 
 * @param session_id Target session's unique identifier
 * @param server_id Server actor handling the session
 * @param response The response message to send
 */
void TopicManagerActor::sendResponse(qb::uuid session_id, qb::ActorId server_id, const std::string& response) {
    sendToSession(session_id, server_id, broker::MessageType::RESPONSE, response);
} 