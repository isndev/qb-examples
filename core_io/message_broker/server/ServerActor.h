/**
 * @file examples/core_io/message_broker/server/ServerActor.h
 * @example Message Broker Server - Session Managing Actor
 * @brief Defines `ServerActor` for managing `BrokerSession` instances and bridging
 *        communication with the `TopicManagerActor`.
 *
 * @details
 * The `ServerActor` handles client connections delegated by `AcceptActor`.
 * - Inherits `qb::Actor` and `qb::io::use<ServerActor>::tcp::io_handler<BrokerSession>`.
 * - `onInit()`: Registers for `NewSessionEvent` and `SendMessageEvent`.
 * - `on(NewSessionEvent&)`: Creates a `BrokerSession` for new client sockets.
 * - `handleSubscribe()`, `handleUnsubscribe()`, `handlePublish()`: These methods are called by
 *   `BrokerSession` instances. They create corresponding events (`SubscribeEvent`,
 *   `UnsubscribeEvent`, `PublishEvent`) and `push` them to the `TopicManagerActor`.
 *   These handlers demonstrate efficient message forwarding, particularly `handlePublish` which
 *   accepts a `broker::MessageContainer&&` and `std::string_view`s to enable zero-copy forwarding.
 * - `handleDisconnect()`: Called by `BrokerSession` upon client disconnection, forwards a
 *   `DisconnectEvent` to `TopicManagerActor`.
 * - `on(SendMessageEvent&)`: Receives messages from `TopicManagerActor` intended for a specific
 *   client, looks up the `BrokerSession`, and sends the message through it.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`.
 * - `qb::io::use<ServerActor>::tcp::io_handler<BrokerSession>`: For managing `BrokerSession`s.
 * - Event Handling: `on(NewSessionEvent&)`, `on(SendMessageEvent&)`.
 * - Inter-Actor Communication: `push<Event>(...)` to `TopicManagerActor`.
 * - Zero-Copy Message Forwarding: Design of `handlePublish` to work with `MessageContainer`
 *   and `string_view`s for efficient data propagation (actual event definitions in `Events.h`).
 */

#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include "../shared/Protocol.h"
#include "../shared/Events.h"
#include "BrokerSession.h"
#include <string_view>

/**
 * @brief Manages a pool of broker sessions and bridges them with TopicManagerActor
 * 
 * ServerActor demonstrates several key QB framework concepts:
 * 1. Multiple inheritance from QB base classes:
 *    - qb::Actor for actor system integration
 *    - qb::io::use<ServerActor>::tcp::io_handler<BrokerSession> for I/O handling
 * 
 * 2. Session Management:
 *    - Creates BrokerSession instances for new connections
 *    - Manages session lifecycle (creation, message handling, cleanup)
 *    - Routes messages between sessions and TopicManagerActor
 * 
 * 3. Event Handling:
 *    - Handles NewSessionEvent from AcceptActor
 *    - Handles SendMessageEvent from TopicManagerActor
 *    - Routes session events to TopicManagerActor
 * 
 * 4. QB I/O Integration:
 *    - Uses QB's I/O framework for TCP communication
 *    - Demonstrates proper session management patterns
 *    - Shows how to handle async I/O in an actor system
 */
class ServerActor : public qb::Actor,
                    public qb::io::use<ServerActor>::tcp::io_handler<BrokerSession> {
private:
    qb::ActorId _topic_manager_id;  // ID of the TopicManager actor for message routing

public:
    /**
     * @brief Constructs a new server actor
     * 
     * @param topic_manager_id ID of the TopicManager actor this server will work with
     * 
     * The ServerActor needs the TopicManager's ID to:
     * 1. Forward subscription requests
     * 2. Route publish messages
     * 3. Notify about client disconnections
     */
    explicit ServerActor(qb::ActorId topic_manager_id);
    
    /**
     * @brief Initializes the server actor
     * 
     * QB Framework calls this when the actor starts. This method:
     * 1. Registers necessary event handlers
     * 2. Sets up any required resources
     * 3. Logs initialization for debugging
     * 
     * @return true if initialization successful, false otherwise
     */
    bool onInit() override;

    /**
     * @brief Handles new client connections
     * 
     * Called by AcceptActor when a new client connects. This method:
     * 1. Creates a new BrokerSession for the client
     * 2. Initializes the session with the received socket
     * 3. Sets up protocol handling for the session
     * 
     * @param evt Event containing the new client's socket
     * 
     * This demonstrates QB's session management pattern where
     * io_handler creates and manages session instances.
     */
    void on(NewSessionEvent& evt);
    
    /**
     * @brief Handles subscription requests from sessions
     * 
     * Called by BrokerSession when a client sends a SUBSCRIBE message.
     * Demonstrates proper event routing in QB with zero-copy optimization:
     * 1. Creates a SubscribeEvent with moved message
     * 2. Sets necessary event fields using string_view references
     * 3. Pushes event to TopicManagerActor
     * 
     * @param session_id ID of the requesting session
     * @param msg Message containing the subscription request, moved to avoid copies
     */
    void handleSubscribe(qb::uuid session_id, broker::Message&& msg);

    /**
     * @brief Handles unsubscription requests from sessions
     * 
     * Called by BrokerSession when a client sends an UNSUBSCRIBE message.
     * Shows QB's event-based communication with zero-copy optimization:
     * 1. Creates an UnsubscribeEvent with moved message
     * 2. Fills event data using string_view references
     * 3. Routes to TopicManagerActor
     * 
     * @param session_id ID of the requesting session
     * @param msg Message containing the unsubscription request, moved to avoid copies
     */
    void handleUnsubscribe(qb::uuid session_id, broker::Message&& msg);

    /**
     * @brief Handles publish requests from sessions
     * 
     * Called by BrokerSession when a client sends a PUBLISH message.
     * Shows QB's event-based communication with zero-copy optimization:
     * 1. Creates a PublishEvent with the MessageContainer
     * 2. Uses string_view references to payload stored in the container
     * 3. Routes to TopicManagerActor for broadcasting
     * 
     * @param session_id ID of the sending session
     * @param container MessageContainer providing ownership of the message data
     * @param topic Topic to publish to (string_view into the container's payload)
     * @param content Message content (string_view into the container's payload)
     */
    void handlePublish(qb::uuid session_id, broker::MessageContainer&& container, 
                       std::string_view topic, std::string_view content);

    /**
     * @brief Handles session disconnections
     * 
     * Called by BrokerSession when a client disconnects.
     * Demonstrates cleanup pattern in QB:
     * 1. Notifies TopicManagerActor via DisconnectEvent
     * 2. Allows for proper subscription cleanup
     * 3. Maintains system consistency
     * 
     * @param session_id ID of the disconnected session
     */
    void handleDisconnect(qb::uuid session_id);
    
    /**
     * @brief Handles message delivery requests from TopicManager
     * 
     * Called when TopicManager wants to send a message to a specific client.
     * Shows QB's bidirectional communication:
     * 1. Receives SendMessageEvent from TopicManager
     * 2. Looks up target session
     * 3. Delivers message using QB's I/O system
     * 
     * @param evt Event containing the target session and message
     */
    void on(SendMessageEvent& evt);
}; 