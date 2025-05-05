/**
 * @file Protocol.cpp
 * @brief Implements QB-integrated message serialization for the broker protocol
 * 
 * This file contains the QB framework integration code that enables:
 * - Efficient binary serialization of broker messages
 * - Zero-copy buffer management
 * - Type-safe message handling
 * 
 * The implementation uses QB's pipe system for optimal performance and
 * memory management during network I/O operations.
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