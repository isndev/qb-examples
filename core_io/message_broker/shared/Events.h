/**
 * @file examples/core_io/message_broker/shared/Events.h
 * @example Message Broker - Shared Event & Message Container Definitions
 * @brief Defines custom `qb::Event` types and a `MessageContainer` for efficient,
 *        zero-copy message handling in the message broker example.
 *
 * @details
 * This header is crucial for the message broker system. It declares:
 * 1.  `broker::MessageContainer`:
 *     -   A class designed to manage the lifetime of `broker::Message` data using a
 *         `std::shared_ptr` with atomic operations for thread-safe reference counting.
 *     -   Enables zero-copy message passing: a single instance of message data can be
 *         safely referenced by multiple events or actors (potentially on different cores)
 *         without copying the payload. String views (`std::string_view`) can then point
 *         into the container's payload.
 *     -   Used by `PublishEvent` and `SendMessageEvent` to hold the actual message content.
 * 2.  `NewSessionEvent`: Sent by `AcceptActor` to `ServerActor` with a new client socket.
 * 3.  `SubscribeEvent`: Sent by `ServerActor` to `TopicManagerActor`.
 *     -   Contains `session_id` and the `broker::MessageContainer` (holding the raw subscribe message,
 *       from which a `topic` string_view is derived).
 * 4.  `UnsubscribeEvent`: Similar to `SubscribeEvent`, for unsubscription requests.
 * 5.  `PublishEvent`: Sent by `ServerActor` to `TopicManagerActor`.
 *     -   Contains `session_id`, the `broker::MessageContainer` (holding the raw publish message),
 *       and `std::string_view`s for `topic` and `content` that point into the container's payload.
 *       This demonstrates efficient passing of message parts without copying.
 * 6.  `SendMessageEvent`: Sent by `TopicManagerActor` to a `ServerActor` to deliver a message
 *     to a specific client. It holds a `broker::MessageContainer`, allowing the message data
 *     to be shared efficiently if broadcast to multiple clients via different `ServerActor`s.
 * 7.  `DisconnectEvent`: Sent by `ServerActor` to `TopicManagerActor` when a client disconnects.
 * 8.  `BrokerInputEvent`: Client-side event from `InputActor` to `ClientActor`, carrying the raw command string.
 *
 * QB Features Demonstrated:
 * - Custom `qb::Event`s for typed, asynchronous communication.
 * - Advanced Message Handling: Use of `MessageContainer` with `std::string_view` to achieve
 *   zero-copy semantics for message payloads passed between actors.
 * - `qb::string<N>` for fixed-size string event fields.
 * - `qb::uuid` for session identification.
 * - `qb::io::tcp::socket` carried by events.
 */

#pragma once

#include <qb/event.h>
#include <qb/io/tcp/socket.h>
#include <qb/string.h>
#include "Protocol.h"
#include <memory>
#include <string_view>
#include <atomic>

namespace broker {

/**
 * @brief Container for managing message lifetimes with thread-safe reference counting
 * 
 * This class provides ownership management for message data, allowing:
 * - Zero-copy string_view references to message contents
 * - Safe lifetime management via atomic shared_ptr
 * - Efficient move semantics for large message payloads
 * - Thread-safe reference counting for multi-core environments
 * 
 * The container ensures that message memory is properly freed when the last 
 * reference is destroyed, even when accessed from different CPU cores.
 */
class MessageContainer {
private:
    // Using std::shared_ptr with atomic operations for thread-safe reference counting
    std::shared_ptr<broker::Message> _message;

public:
    /**
     * @brief Default constructor
     */
    MessageContainer() = default;
    
    /**
     * @brief Copy constructor with atomic reference counting
     * 
     * Uses atomic operations to maintain thread-safe reference counting
     * when sharing message data between actors on different CPU cores.
     * This ensures proper memory cleanup when the last reference is destroyed.
     * 
     * @param other The container to share data with
     */
    MessageContainer(const MessageContainer& other) 
        : _message(std::atomic_load(&other._message)) {}
    
    /**
     * @brief Copy assignment with atomic reference counting
     * 
     * @param other The container to share data with
     * @return Reference to this container
     */
    MessageContainer& operator=(const MessageContainer& other) {
        if (this != &other) {
            std::atomic_store(&_message, std::atomic_load(&other._message));
        }
        return *this;
    }
    
    /**
     * @brief Constructs from an existing message
     * 
     * Takes ownership of the message data, allowing string_view
     * references to safely point to the message contents.
     * 
     * @param msg The message to take ownership of
     */
    explicit MessageContainer(broker::Message&& msg)
        : _message(std::make_shared<broker::Message>(std::move(msg))) {}
        
    /**
     * @brief Constructs a new message from type and payload
     * 
     * Creates a new message and takes ownership, optimized for
     * move semantics with large payloads.
     * 
     * @param type The message type
     * @param payload The message payload
     */
    MessageContainer(broker::MessageType type, std::string payload)
        : _message(std::make_shared<broker::Message>(type, std::move(payload))) {}
    
    /**
     * @brief Gets the message type
     * @return Message type enum value
     */
    broker::MessageType type() const { 
        auto msg = std::atomic_load(&_message);
        return msg ? msg->type : broker::MessageType::ERROR;
    }
    
    /**
     * @brief Gets the message payload as string_view
     * 
     * Returns a view into the owned message payload, avoiding
     * copies while maintaining safe lifetime management.
     * 
     * @return View of the message payload
     */
    std::string_view payload() const {
        auto msg = std::atomic_load(&_message);
        return msg ? std::string_view(msg->payload) : std::string_view{};
    }
    
    /**
     * @brief Access the underlying message
     * 
     * Provides access to the raw message for serialization
     * or other operations requiring the complete message.
     * 
     * @return Const reference to the message
     */
    const broker::Message& message() const {
        static const broker::Message empty_msg{};
        auto msg = std::atomic_load(&_message);
        return msg ? *msg : empty_msg;
    }
    
    /**
     * @brief Checks if the container holds a valid message
     * @return true if container has a message
     */
    bool valid() const { 
        return std::atomic_load(&_message) != nullptr; 
    }
    
    /**
     * @brief Implicit conversion to bool for validity checks
     * @return true if container has a valid message
     */
    operator bool() const { return valid(); }
};

} // namespace broker

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
 * @brief Event for topic subscription requests
 * 
 * Flow:
 * 1. Client sends SUBSCRIBE message
 * 2. ServerActor creates SubscribeEvent
 * 3. TopicManagerActor processes subscription:
 *    - Registers client for the topic
 *    - Sends confirmation response
 * 
 * The event source (evt.getSource()) contains the ServerActor's ID,
 * enabling TopicManagerActor to route responses back to the correct client.
 */
struct SubscribeEvent : public qb::Event {
    qb::uuid session_id;                     ///< Unique session identifier
    broker::MessageContainer message_data;   ///< Message container with shared ownership
    std::string_view topic;                  ///< View of topic from message payload
    
    /**
     * @brief Creates subscription event from session and message
     * 
     * Optimized for zero-copy by using views into the message payload
     * while maintaining safe lifetime through shared ownership.
     * 
     * @param id Session identifier
     * @param msg Message to process
     */
    SubscribeEvent(qb::uuid id, broker::Message&& msg)
        : session_id(id), 
          message_data(std::move(msg)),
          topic(message_data.payload()) {}
    
    /**
     * @brief Default constructor for QB event system
     */
    SubscribeEvent() = default;
};

/**
 * @brief Event for topic unsubscription requests
 * 
 * Flow:
 * 1. Client sends UNSUBSCRIBE message
 * 2. ServerActor creates UnsubscribeEvent
 * 3. TopicManagerActor processes unsubscription:
 *    - Removes client from the topic
 *    - Sends confirmation response
 * 
 * The event source (evt.getSource()) identifies the ServerActor
 * that was handling the client.
 */
struct UnsubscribeEvent : public qb::Event {
    qb::uuid session_id;                     ///< Unique session identifier
    broker::MessageContainer message_data;   ///< Message container with shared ownership
    std::string_view topic;                  ///< View of topic from message payload
    
    /**
     * @brief Creates unsubscription event from session and message
     * 
     * Optimized for zero-copy by using views into the message payload
     * while maintaining safe lifetime through shared ownership.
     * 
     * @param id Session identifier
     * @param msg Message to process
     */
    UnsubscribeEvent(qb::uuid id, broker::Message&& msg)
        : session_id(id), 
          message_data(std::move(msg)),
          topic(message_data.payload()) {}
    
    /**
     * @brief Default constructor for QB event system
     */
    UnsubscribeEvent() = default;
};

/**
 * @brief Event for publishing messages to topics
 * 
 * Flow:
 * 1. Client sends PUBLISH message
 * 2. ServerActor creates PublishEvent
 * 3. TopicManagerActor processes publication:
 *    - Finds all topic subscribers
 *    - Broadcasts message to subscribers
 * 
 * Uses zero-copy techniques with shared ownership to efficiently
 * handle message content without unnecessary copying.
 */
struct PublishEvent : public qb::Event {
    qb::uuid session_id;                     ///< Message sender's session ID
    broker::MessageContainer message_data;   ///< Message container with shared ownership
    std::string_view topic;                  ///< View of topic from message payload
    std::string_view content;                ///< View of message content
    
    /**
     * @brief Default constructor for QB event system
     */
    PublishEvent() = default;
    
    /**
     * @brief Creates publish event from pre-parsed message
     * 
     * Used when message was already parsed into topic and content parts,
     * but still maintains shared ownership for safety.
     * 
     * @param id Session identifier
     * @param msg Original message
     * @param t Topic string view
     * @param c Content string view
     */
    PublishEvent(qb::uuid id, broker::Message&& msg, std::string_view t, std::string_view c)
        : session_id(id), 
          message_data(std::move(msg)),
          topic(t),
          content(c) {}
          
    /**
     * @brief Creates publish event from a MessageContainer
     * 
     * This constructor is designed to work with pre-created MessageContainer,
     * allowing safe use of string_view references.
     * 
     * @param id Session identifier
     * @param container Message container with ownership
     * @param t Topic string view (must be view into container)
     * @param c Content string view (must be view into container)
     */
    PublishEvent(qb::uuid id, broker::MessageContainer&& container, 
                 std::string_view t, std::string_view c)
        : session_id(id), 
          message_data(std::move(container)),
          topic(t),
          content(c) {}
};

/**
 * @brief Event for targeted message delivery
 * 
 * Flow:
 * 1. TopicManagerActor creates SendMessageEvent
 * 2. Routes to specific ServerActor
 * 3. ServerActor sends through client's socket
 * 
 * Optimized to use shared ownership for message data to avoid
 * unnecessary copying during event routing.
 */
struct SendMessageEvent : public qb::Event {
    qb::uuid session_id;                     ///< Target client's session ID
    broker::MessageContainer message_data;   ///< Message container with shared ownership
    
    /**
     * @brief Creates message delivery event
     * 
     * @param id Target session
     * @param type Message type
     * @param payload Message content
     */
    SendMessageEvent(qb::uuid id, broker::MessageType type, std::string payload)
        : session_id(id),
          message_data(type, std::move(payload)) {}
    
    /**
     * @brief Creates message delivery event with a shared message container
     * 
     * This constructor enables atomic sharing of the same message data
     * between multiple recipients. By sharing the underlying message container,
     * we avoid copying message data even when broadcasting to many clients.
     * 
     * @param id Target session
     * @param shared_container A shared pointer to an existing message container
     */
    SendMessageEvent(qb::uuid id, const broker::MessageContainer& shared_container)
        : session_id(id),
          message_data(shared_container) {}
    
    /**
     * @brief Default constructor for QB event system
     */
    SendMessageEvent() = default;
    
    /**
     * @brief Gets the message to deliver
     * @return Const reference to the message
     */
    const broker::Message& message() const {
        return message_data.message();
    }
};

/**
 * @brief Event for client disconnection handling
 * 
 * Flow:
 * 1. Client disconnects or timeout occurs
 * 2. ServerActor detects disconnection
 * 3. TopicManagerActor processes cleanup:
 *    - Removes from all topic subscriptions
 *    - Cleans up session resources
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
 * 1. User enters command in client
 * 2. InputActor creates BrokerInputEvent
 * 3. ClientActor processes input:
 *    - Parses command format
 *    - Formats as protocol message
 *    - Sends to server if connected
 * 
 * This event separates input handling from network I/O,
 * enabling clean separation of concerns in the client.
 */
struct BrokerInputEvent : public qb::Event {
    qb::string<1024> command;    ///< User input command (max 1024 chars)
}; 