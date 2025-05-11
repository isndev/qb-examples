/**
 * @file examples/core_io/chat_tcp/shared/Protocol.cpp
 * @example TCP Chat Server/Client - Shared Protocol Serialization
 * @brief Implements the serialization logic for the custom `chat::Message` type
 *        into QB-IO's pipe buffers.
 *
 * @details
 * This file contains the template specialization of `qb::allocator::pipe<char>::put<chat::Message>()`.
 * This specialization is crucial for QB-IO to understand how to convert a `chat::Message`
 * object into a byte stream according to the defined chat protocol format when an actor
 * uses the `*this << message_object;` syntax for sending.
 *
 * Serialization Process:
 * 1.  A `chat::MessageHeader` is constructed with the correct magic number, version,
 *     message type (from `msg.type`), and payload length (from `msg.payload.size()`).
 * 2.  The 8-byte header is written to the QB pipe buffer using `this->put(header_ptr, header_size)`.
 * 3.  If the `msg.payload` is not empty, its contents are written to the pipe buffer immediately
 *     after the header using `this->put(payload_data_ptr, payload_size)`.
 *
 * This allows for efficient, typed message sending within the QB-IO framework using the
 * custom chat protocol.
 *
 * QB Features Demonstrated:
 * - `qb::allocator::pipe<char>::put<T>()`: Specialization for custom type serialization.
 * - Writing structured data (header) and raw byte data (payload) into a QB pipe.
 * - Integration with a custom-defined protocol (`chat::Message`, `chat::MessageHeader`).
 */

#include "Protocol.h"

namespace qb::allocator {

/**
 * @brief Template specialization for serializing chat::Message
 * 
 * This implementation provides efficient binary serialization of chat messages
 * into QB's pipe buffer system. The serialization follows this format:
 * 
 * Message Layout:
 * +----------------+------------------+
 * |     Header    |     Payload      |
 * | (8 bytes)     | (variable size)  |
 * +----------------+------------------+
 * 
 * Header Format:
 * - Magic (2 bytes)    : 'QC' (0x5143)
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
pipe<char>& pipe<char>::put<chat::Message>(const chat::Message& msg) {
    constexpr size_t HEADER_SIZE = sizeof(chat::MessageHeader);
    
    // Construct the header with proper protocol identifiers
    chat::MessageHeader header{
        chat::PROTOCOL_MAGIC,     // Identifies this as a chat protocol message
        chat::PROTOCOL_VERSION,   // Current protocol version
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