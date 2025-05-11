/**
 * @file examples/core_io/chat_tcp/shared/Protocol.h
 * @example TCP Chat Server/Client - Shared Protocol Definition
 * @brief Defines the simple text-based chat protocol, including message structure,
 * serialization, and parsing logic for QB-IO integration.
 *
 * @details
 * This file specifies a custom protocol for the TCP chat application.
 * **Protocol Format**:
 * A simple header is used for message framing:
 * - Magic (uint16_t `0x5143` 'QC'): Identifies QB Chat protocol.
 * - Version (uint8_t `0x01`): Protocol version.
 * - Type (uint8_t `chat::MessageType`): Defines the kind of message (e.g., AUTH_REQUEST, CHAT_MESSAGE).
 * - Length (uint32_t): Size of the string payload that follows.
 * The header is followed by the UTF-8 string payload.
 *
 * **Components**:
 * - `chat::MessageHeader`: Struct representing the 8-byte protocol header.
 * - `chat::MessageType`: Enum for different message types.
 * - `chat::Message`: Struct holding the `MessageType` and `std::string payload` for an application-level message.
 * - `qb::allocator::pipe<char>::put<chat::Message>`: A template specialization that serializes
 *   a `chat::Message` object into the defined binary format (header + payload) into a QB pipe buffer.
 *   This enables sending with `*this << chat_message_object;`.
 * - `chat::ChatProtocol<IO_>`: A class template inheriting from `qb::io::async::AProtocol<IO_>`.
 *   This class implements the server-side and client-side logic for parsing the byte stream:
 *     - `getMessageSize()`: Reads the header, validates magic/version, and returns the total expected
 *       message size (header + payload_length). If not enough data is available for the header or
 *       the full payload, it returns 0, indicating more data is needed.
 *     - `onMessage()`: Called when a complete message is buffered. It reconstructs the `chat::Message`
 *       object from the header and payload data and passes it to the `IO_` handler (e.g., `ClientActor`
 *       or `ChatSession`) via `this->_io.on(parsed_message)`.
 *     - `reset()`: Resets the protocol parsing state, e.g., after an error.
 *
 * QB Features Demonstrated:
 * - Custom Protocol Design: Defining a binary header for message framing.
 * - `qb::io::async::AProtocol<IO_>`: Implementing a custom protocol handler for QB-IO.
 *   - Reading from input buffer: `this->_io.in()`.
 *   - Dispatching parsed messages: `this->_io.on(message)`.
 * - `qb::allocator::pipe`: Specializing `put<T>()` for custom message type serialization, enabling
 *   efficient, low-copy sending.
 * - CRTP (Curiously Recurring Template Pattern): Used by `AProtocol`.
 */

#pragma once

#include <qb/io/async.h>
#include <string>
#include <vector>
#include <cstring>
#include <qb/system/allocator/pipe.h>

/**
 * @brief Namespace containing all chat-related protocol definitions
 * 
 * This namespace encapsulates all protocol-specific types and constants to avoid
 * naming conflicts and provide a clear scope for chat functionality.
 */
namespace chat {

/**
 * @brief Header structure for the chat protocol messages
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
 * | 0      | 2    | Magic number (QC) |
 * | 2      | 1    | Version number    |
 * | 3      | 1    | Message type      |
 * | 4      | 4    | Payload length    |
 */
struct MessageHeader {
    uint16_t magic;    // 'QC' (QB Chat)
    uint8_t version;   // Protocol version
    uint8_t type;      // Message type
    uint32_t length;   // Payload length
};

// Protocol constants with detailed explanations
/// Magic number 'QC' (0x5143) identifies this as a QB Chat protocol message
constexpr uint16_t PROTOCOL_MAGIC = 0x5143;  
/// Current protocol version, increment for breaking changes
constexpr uint8_t PROTOCOL_VERSION = 0x01;    

/**
 * @brief Enumeration of all possible message types in our chat protocol
 * 
 * Each type represents a distinct kind of message that can be exchanged
 * between client and server. The protocol is designed to be extensible,
 * allowing new message types to be added in future versions.
 */
enum class MessageType : uint8_t {
    AUTH_REQUEST = 1,  ///< Client -> Server: Request to join with username
    AUTH_RESPONSE,     ///< Server -> Client: Authentication result
    CHAT_MESSAGE,      ///< Bidirectional: Regular chat message
    USER_LIST,         ///< Server -> Client: List of active users
    ERROR             ///< Server -> Client: Error notification
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
        : type(t), payload(std::move(p)) {}
};

} // namespace chat

/**
 * @brief QB framework integration for message serialization
 * 
 * This namespace contains specializations that tell QB how to handle
 * our custom Message type within its serialization system.
 */
namespace qb::allocator {

template<>
pipe<char>& pipe<char>::put<chat::Message>(const chat::Message& msg);

} // namespace qb::allocator

namespace chat {

/**
 * @brief Protocol implementation using QB's CRTP pattern
 * 
 * This class implements the chat protocol using QB's async I/O framework.
 * It provides:
 * - Message framing and boundary detection
 * - Protocol state management
 * - Automatic message dispatching
 * - Error handling and recovery
 * 
 * The implementation follows QB's CRTP pattern for zero-overhead abstractions
 * while maintaining type safety and compile-time polymorphism.
 * 
 * @tparam IO_ The I/O handler type (typically a Session class)
 */
template<typename IO_>
class ChatProtocol : public qb::io::async::AProtocol<IO_> {
private:
    static constexpr size_t HEADER_SIZE = sizeof(MessageHeader);
    bool _reading_header = true;  ///< Current reading state
    MessageHeader _header{};      ///< Buffer for current message header
    std::vector<char> _payload;   ///< Buffer for message payload

public:
    using message = Message;  ///< Type alias required by QB framework

    /**
     * @brief Constructs protocol handler for given I/O object
     * @param io The I/O handler to use
     */
    explicit ChatProtocol(IO_& io) noexcept 
        : qb::io::async::AProtocol<IO_>(io) {}

    /**
     * @brief Determines the size of the next complete message
     * 
     * This method is called by QB to determine if we have a complete message.
     * It implements our protocol's framing logic:
     * 1. Read the header (if in header reading state)
     * 2. Validate the header
     * 3. Return the total message size (header + payload)
     * 
     * @return Size of complete message, or 0 if more data needed
     */
    std::size_t getMessageSize() noexcept override {
        auto& buffer = this->_io.in();
        
        if (buffer.empty()) return 0;

        if (_reading_header) {
            if (buffer.size() < HEADER_SIZE) return 0;

            std::memcpy(&_header, buffer.cbegin(), HEADER_SIZE);

            // Validate protocol magic and version
            if (_header.magic != PROTOCOL_MAGIC || 
                _header.version != PROTOCOL_VERSION) {
                reset();
                return 0;
            }

            _reading_header = false;
            _payload.resize(_header.length);

            return HEADER_SIZE + _header.length;
        }

        return HEADER_SIZE + _header.length;
    }

    /**
     * @brief Processes a complete message
     * 
     * Called by QB when a complete message is available.
     * This method:
     * 1. Extracts the payload from the buffer
     * 2. Constructs a Message object
     * 3. Passes it to the I/O handler's on() method
     * 
     * @param size Total size of the message (header + payload)
     */
    void onMessage(std::size_t size) noexcept override {
        auto& buffer = this->_io.in();

        // Copy payload if present
        if (_header.length > 0) {
            std::memcpy(_payload.data(), 
                       buffer.cbegin() + HEADER_SIZE, 
                       _header.length);
        }

        // Construct and dispatch message
        Message msg;
        msg.type = static_cast<MessageType>(_header.type);
        if (_header.length > 0) {
            msg.payload.assign(_payload.data(), _header.length);
        }

        this->_io.on(msg);
        _reading_header = true;  // Ready for next message
    }

    /**
     * @brief Resets the protocol state
     * 
     * Called when an error occurs or when we need to resynchronize.
     */
    void reset() noexcept override {
        _reading_header = true;
        _payload.clear();
    }
};

} // namespace chat 