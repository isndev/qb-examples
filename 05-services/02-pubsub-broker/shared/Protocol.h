/**
 * @file examples/05-services/02-pubsub-broker/shared/Protocol.h
 * @example Message Broker - Shared Protocol Definition
 * @brief Defines the custom binary protocol for the message broker system,
 *        including message structure, serialization, and parsing logic for QB-IO.
 *
 * @details
 * This file specifies the network protocol for communication between message broker
 * clients and the server.
 *
 * **Protocol Format**:
 * A fixed-size header precedes a variable-length payload:
 * - Magic (uint16_t `0x514D` 'QM'): Identifies QB Message Broker protocol.
 * - Version (uint8_t `0x01`): Protocol version.
 * - Type (uint8_t `broker::MessageType`): Defines message type (SUBSCRIBE, PUBLISH, MESSAGE, etc.).
 * - Length (uint32_t): Size of the string payload.
 * The header (8 bytes) is followed by the UTF-8 string payload.
 *
 * **Components**:
 * - `broker::MessageHeader`: Struct for the 8-byte protocol header.
 * - `broker::MessageType`: Enum for message types.
 * - `broker::Message`: Struct for application-level message representation (type, payload string).
 * - `qb::allocator::pipe<char>::put<broker::Message>`: (Defined in Protocol.cpp) Template specialization
 *   for serializing `broker::Message` into the binary format for QB pipe buffers.
 * - `broker::BrokerProtocol<IO_>`: Class template deriving from `qb::io::async::AProtocol<IO_>`.
 *   - `getMessageSize()`: Reads and validates the header, returns total expected message size.
 *   - `onMessage()`: Reconstructs `broker::Message` from the buffer and dispatches it to the
 *     `IO_` handler (e.g., `ClientActor` or `BrokerSession`) via `this->_io.on(std::move(parsed_message))`
 *     (using `std::move` for potential optimization if payload is large).
 *   - `reset()`: No-op because framing is stateless; invalid headers terminate the protocol.
 *
 * QB Features Demonstrated:
 * - Custom Protocol Design: Binary header for message framing.
 * - `qb::io::async::AProtocol<IO_>`: Base for custom protocol handlers.
 *   - Input buffer access: `this->_io.in()`.
 *   - Parsed message dispatch: `this->_io.on(message)`.
 * - `qb::allocator::pipe`: `put<T>()` specialization for custom type serialization (see .cpp).
 * - Efficient Message Handling: Passing parsed messages with `std::move` in `onMessage()`.
 */

#pragma once

#include <qb/io/async.h>
#include <string>
#include <cstring>
#include <qb/system/allocator/pipe.h>

/**
 * @brief Namespace containing all broker-related protocol definitions
 *
 * This namespace encapsulates all protocol-specific types and constants to avoid
 * naming conflicts and provide a clear scope for broker functionality.
 */
namespace broker {

/**
 * @brief Header structure for the broker protocol messages
 *
 * Each message in our protocol starts with this header, followed by the payload.
 * The header ensures:
 * - Protocol identification via magic number
 * - Version compatibility checking
 * - Message type identification
 * - Safe payload handling with length
 *
 * Memory layout (8 bytes total):
 * | Offset | Size | Description        |
 * |--------|------|-------------------|
 * | 0      | 2    | Magic number (QM) |
 * | 2      | 1    | Version number    |
 * | 3      | 1    | Message type      |
 * | 4      | 4    | Payload length    |
 */
struct MessageHeader {
    uint16_t magic;   // 'QM' (QB Message broker)
    uint8_t  version; // Protocol version
    uint8_t  type;    // Message type
    uint32_t length;  // Payload length
};

// Protocol constants with detailed explanations
/// Magic number 'QM' (0x514D) identifies this as a QB Message broker protocol message
constexpr uint16_t PROTOCOL_MAGIC = 0x514D;
/// Current protocol version, increment for breaking changes
constexpr uint8_t PROTOCOL_VERSION = 0x01;

/**
 * @brief Enumeration of all possible message types in our broker protocol
 *
 * Each type represents a distinct kind of message that can be exchanged
 * between client and server. The protocol is designed to be extensible,
 * allowing new message types to be added in future versions.
 */
enum class MessageType : uint8_t {
#undef ERROR
    SUBSCRIBE = 1, ///< Client -> Server: Subscribe to a topic
    UNSUBSCRIBE,   ///< Client -> Server: Unsubscribe from a topic
    PUBLISH,       ///< Client -> Server: Publish message to a topic
    MESSAGE,       ///< Server -> Client: Message from a topic
    RESPONSE,      ///< Server -> Client: Response to a command
    ERROR          ///< Server -> Client: Error notification
};

/**
 * @brief Basic message structure used for serialization
 *
 * This structure represents a complete protocol message including:
 * - Type information for message handling
 * - Payload data as a string
 *
 * The message is designed to be:
 * - Easy to serialize/deserialize
 * - Memory efficient
 * - Type-safe through MessageType enum
 */
struct Message {
    MessageType type;
    std::string payload;

    Message() = default;

    /**
     * @brief Constructs a message with type and payload
     * @param t The message type
     * @param p The message payload
     */
    Message(MessageType t, std::string p)
        : type(t)
        , payload(std::move(p)) {}
};

} // namespace broker

/**
 * @brief QB framework integration for message serialization
 *
 * This namespace contains specializations that tell QB how to handle
 * our custom Message type within its serialization system.
 */
namespace qb::allocator {

template <>
pipe<char> &pipe<char>::put<broker::Message>(const broker::Message &msg);

} // namespace qb::allocator

namespace broker {

/**
 * @brief Protocol implementation using QB's CRTP pattern
 *
 * This class implements the broker protocol using QB's async I/O framework.
 * It provides:
 * - Message framing and boundary detection
 * - Protocol state management
 * - Automatic message dispatching
 * - Terminal handling of invalid or oversized frames
 *
 * The implementation follows QB's CRTP pattern for zero-overhead abstractions
 * while maintaining type safety and compile-time polymorphism.
 *
 * @tparam IO_ The I/O handler type (typically a Session class)
 */
template <typename IO_>
class BrokerProtocol : public qb::io::async::AProtocol<IO_> {
private:
    static constexpr std::size_t HEADER_SIZE      = sizeof(MessageHeader);
    static constexpr std::size_t MAX_PAYLOAD_SIZE = 64 * 1024;

public:
    using message = Message; ///< Type alias required by QB framework

    /**
     * @brief Constructs protocol handler for given I/O object
     * @param io The I/O handler to use
     */
    explicit BrokerProtocol(IO_ &io) noexcept
        : qb::io::async::AProtocol<IO_>(io) {}

    /**
     * @brief Determines the size of the next complete message
     *
     * This method is called by QB to determine if we have a complete message.
     * It implements our protocol's framing logic:
     * 1. Inspect and validate the header
     * 2. Reject payloads beyond the application or I/O limits
     * 3. Return the total size only when the full frame is buffered
     *
     * @return Size of complete message, or 0 if more data needed
     */
    std::size_t
    getMessageSize() noexcept override {
        auto &buffer = this->_io.in();

        if (buffer.size() < HEADER_SIZE)
            return qb::io::async::IProtocol::kNoMessage;

        MessageHeader header{};
        std::memcpy(&header, buffer.cbegin(), HEADER_SIZE);

        if (header.magic != PROTOCOL_MAGIC || header.version != PROTOCOL_VERSION) {
            this->not_ok();
            return qb::io::async::IProtocol::kNoMessage;
        }

        const auto max_message_size = this->_io.max_message_size();
        if (header.length > MAX_PAYLOAD_SIZE || max_message_size < HEADER_SIZE || header.length > max_message_size - HEADER_SIZE) {
            this->not_ok();
            return qb::io::async::IProtocol::kNoMessage;
        }

        const auto total_size = HEADER_SIZE + static_cast<std::size_t>(header.length);
        return buffer.size() < total_size ? qb::io::async::IProtocol::kNoMessage : total_size;
    }

    /**
     * @brief Processes a complete message
     *
     * Called by QB when a complete message is available.
     * This method:
     * 1. Extracts the payload from the buffer
     * 2. Constructs a Message object
     * 3. Passes it to the I/O handler's on() method with move semantics
     *    to avoid unnecessary copying
     *
     * @param size Total size of the message (header + payload)
     */
    void
    onMessage(std::size_t size) noexcept override {
        auto &buffer = this->_io.in();

        MessageHeader header{};
        std::memcpy(&header, buffer.cbegin(), HEADER_SIZE);

        Message msg{static_cast<MessageType>(header.type), std::string(buffer.cbegin() + HEADER_SIZE, buffer.cbegin() + size)};

        // Use move semantics to avoid copying the message
        this->_io.on(std::move(msg));
    }

    /**
     * @brief Resets the protocol state
     *
     * The framer keeps no partial state; invalid input is terminal via not_ok().
     */
    void
    reset() noexcept override {}
};

} // namespace broker
