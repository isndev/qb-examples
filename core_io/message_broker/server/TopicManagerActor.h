/**
 * @file TopicManagerActor.h
 * @brief Central actor managing topics and subscriptions
 * 
 * This file demonstrates:
 * 1. How to implement centralized state management in QB
 * 2. How to handle inter-actor communication
 * 3. How to manage shared resources in an actor system
 * 4. How to implement publish-subscribe patterns in QB
 */
#pragma once

#include <qb/actor.h>
#include <map>
#include <set>
#include <string>
#include <string_view>
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
};

/**
 * @brief Actor responsible for managing topics, subscriptions, and message distribution
 * 
 * TopicManagerActor demonstrates QB's state management patterns:
 * 1. Centralized State Management:
 *    - Maintains topic and subscription mappings
 *    - Ensures consistency across the system
 *    - Handles concurrent access safely
 * 
 * 2. Event Processing:
 *    - Handles subscription requests
 *    - Processes message publications
 *    - Manages user sessions
 * 
 * 3. Message Broadcasting:
 *    - Implements efficient message distribution
 *    - Routes messages to correct servers
 *    - Handles subscriber notifications
 * 
 * 4. Resource Management:
 *    - Tracks active sessions
 *    - Manages topic subscriptions
 *    - Handles cleanup on disconnection
 */
class TopicManagerActor : public qb::Actor {
private:
    // Maps session_id to SessionInfo
    std::map<qb::uuid, SessionInfo> _sessions;
    
    // Maps topic to set of session_ids subscribed to that topic
    std::map<std::string, std::set<qb::uuid>> _subscriptions;
    
    // Maps session_id to set of topics they're subscribed to (for quick cleanup)
    std::map<qb::uuid, std::set<std::string>> _session_topics;

public:
    /**
     * @brief Default constructor
     * 
     * QB actors should have minimal construction logic.
     * Main initialization happens in onInit().
     */
    TopicManagerActor() = default;

    /**
     * @brief Initializes the topic manager actor
     * 
     * QB initialization sequence:
     * 1. Registers event handlers for:
     *    - SubscribeEvent: Topic subscription
     *    - UnsubscribeEvent: Topic unsubscription
     *    - PublishEvent: Message publishing
     *    - DisconnectEvent: Session cleanup
     * 2. Sets up internal state
     * 3. Prepares for message handling
     * 
     * @return true if initialization successful
     */
    bool onInit() override;

    /**
     * @brief Handles topic subscription requests with zero-copy optimization
     * 
     * Demonstrates QB's event handling pattern with optimization:
     * 1. Event Processing:
     *    - Uses string_view for zero-copy access to topic
     *    - Validates session exists
     *    - Updates topic subscriptions
     *    - Sends response to client
     * 
     * 2. State Management:
     *    - Updates _subscriptions map
     *    - Updates _session_topics map
     *    - Maintains consistency
     * 
     * @param evt Subscription event containing session info and topic as string_view
     */
    void on(SubscribeEvent& evt);

    /**
     * @brief Handles topic unsubscription requests with zero-copy optimization
     * 
     * Demonstrates QB's state management pattern with optimization:
     * 1. State Management:
     *    - Uses string_view for zero-copy access to topic
     *    - Validates session and topic exist
     *    - Removes from _subscriptions map
     *    - Updates _session_topics map
     * 
     * 2. Response Handling:
     *    - Sends confirmation to client
     *    - Handles error cases
     * 
     * @param evt Unsubscription event containing session info and topic as string_view
     */
    void on(UnsubscribeEvent& evt);

    /**
     * @brief Handles message publication requests with zero-copy optimization
     * 
     * Demonstrates QB's message broadcasting with optimization:
     * 1. Message Processing:
     *    - Uses string_view for zero-copy access to topic and content
     *    - Validates topic exists
     *    - Formats message for delivery
     * 
     * 2. Message Distribution:
     *    - Finds all topic subscribers
     *    - Routes messages to correct servers
     *    - Delivers to each subscriber
     * 
     * @param evt Publish event containing topic and message content as string_views
     */
    void on(PublishEvent& evt);

    /**
     * @brief Handles client disconnections
     * 
     * Demonstrates QB's cleanup patterns:
     * 1. State Cleanup:
     *    - Removes from _sessions
     *    - Removes from all topic subscriptions
     *    - Maintains consistency
     * 
     * 2. Resource Management:
     *    - Cleans up session data
     *    - Updates topic subscribers
     * 
     * @param evt Disconnect event containing the session ID
     */
    void on(DisconnectEvent& evt);

private:
    /**
     * @brief Sends a message to a specific session with optimized delivery
     * 
     * Demonstrates QB's targeted message delivery with optimization:
     * 1. Creates SendMessageEvent with message type and payload
     * 2. Routes to correct ServerActor
     * 3. Handles delivery
     * 
     * @param session_id Target session ID
     * @param server_id Server managing the target session
     * @param type Type of message to send
     * @param payload Content of the message
     */
    void sendToSession(qb::uuid session_id, qb::ActorId server_id, 
                      broker::MessageType type, const std::string& payload);

    /**
     * @brief Sends a message to a specific session using a shared message container
     * 
     * This specialized method enables atomic sharing of message data between
     * multiple recipients when broadcasting a message:
     * 1. Reuses the same message container across all subscribers
     * 2. Avoids duplicate message allocations
     * 3. Provides thread-safe cleanup via shared_ptr reference counting
     * 
     * @param session_id Target session ID
     * @param server_id Server managing the target session
     * @param shared_message Shared message container with atomic reference counting
     */
    void sendToSession(qb::uuid session_id, qb::ActorId server_id,
                      const broker::MessageContainer& shared_message);

    /**
     * @brief Sends an error message to a specific session
     * 
     * Demonstrates QB's error handling pattern:
     * 1. Creates error message
     * 2. Sets appropriate type
     * 3. Routes to client
     * 
     * @param session_id Target session ID
     * @param server_id Server managing the target session
     * @param error Error message to send
     */
    void sendError(qb::uuid session_id, qb::ActorId server_id, const std::string& error);

    /**
     * @brief Sends a response message to a specific session
     * 
     * Demonstrates QB's response pattern:
     * 1. Creates response message
     * 2. Sets appropriate type
     * 3. Routes to client
     * 
     * @param session_id Target session ID
     * @param server_id Server managing the target session
     * @param response Response message to send
     */
    void sendResponse(qb::uuid session_id, qb::ActorId server_id, const std::string& response);
}; 