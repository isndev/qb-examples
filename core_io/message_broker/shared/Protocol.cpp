/**
 * @file examples/core_io/message_broker/shared/Protocol.cpp
 * @example Message Broker - Shared Protocol Serialization Implementation
 * @brief Implements the serialization logic for the custom `broker::Message` type
 *        into QB-IO's pipe buffers for the message broker system.
 *
 * @details
 * This file provides the template specialization of `qb::allocator::pipe<char>::put<broker::Message>()`.
 * This function defines how a `broker::Message` object is converted into a byte stream
 * according to the message broker's custom binary protocol when an actor sends such a message
 * using the `*this << message_object;` syntax.
 *
 * Serialization Process:
 * 1.  A `broker::MessageHeader` is constructed. It includes:
 *     -   `broker::PROTOCOL_MAGIC` (0x514D).
 *     -   `broker::PROTOCOL_VERSION` (0x01).
 *     -   The message type from `msg.type`.
 *     -   The payload length from `msg.payload.size()`.
 * 2.  This 8-byte header is written to the QB pipe buffer.
 * 3.  If the `msg.payload` is not empty, its string data is then written to the pipe buffer
 *     immediately following the header.
 *
 * This mechanism allows QB-IO to efficiently serialize and send custom message types over the network.
 *
 * QB Features Demonstrated:
 * - `qb::allocator::pipe<char>::put<T>()`: Template specialization for custom type serialization.
 * - Writing data to QB pipe buffers: `this->put(pointer_to_data, size_of_data)`.
 * - Integration with a custom-defined protocol (`broker::Message`, `broker::MessageHeader`).
 */

#include "Protocol.h"

namespace qb::allocator {

/**
 * @brief Template specialization for serializing broker::Message
 * 
 * This implementation provides efficient binary serialization of broker messages
 * into QB's pipe buffer system. The serialization follows this format:
 * 
 * Message Layout:
 * +----------------+------------------+
 * |     Header    |     Payload      |
 * | (8 bytes)     | (variable size)  |
 * +----------------+------------------+
 * 
 * Header Format:
 * - Magic (2 bytes)    : 'QM' (0x514D)
 * - Version (1 byte)   : 0x01
 * - Type (1 byte)      : MessageType enum value
 * - Length (4 bytes)   : Payload size in bytes
 * 
 * Performance considerations:
 * - Uses direct memory operations for efficiency
 * - Minimizes memory copies
 * - Ensures proper alignment
 * 
 * @param msg The message to serialize
 * @return Reference to the pipe for method chaining
 */
template<>
pipe<char>& pipe<char>::put<broker::Message>(const broker::Message& msg) {
    constexpr size_t HEADER_SIZE = sizeof(broker::MessageHeader);
    
    // Construct the header with proper protocol identifiers
    broker::MessageHeader header{
        broker::PROTOCOL_MAGIC,     // Identifies this as a broker protocol message
        broker::PROTOCOL_VERSION,   // Current protocol version
        static_cast<uint8_t>(msg.type),  // Message type for routing
        static_cast<uint32_t>(msg.payload.size())  // Payload size for framing
    };
    
    // Write header to pipe using zero-copy where possible
    this->put(reinterpret_cast<const char*>(&header), HEADER_SIZE);

    // Write payload if present (optimization for empty messages)
    if (!msg.payload.empty()) {
        this->put(msg.payload.data(), msg.payload.size());
    }
    return *this;
}

} // namespace qb::allocator 