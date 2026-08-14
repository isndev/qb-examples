/**
 * @file examples/05-services/01-tcp-chat/shared/Events.h
 * @example TCP Chat Server/Client - Shared Event Definitions
 * @brief Defines the custom `qb::Event` types used for communication between actors
 * in the TCP chat system, particularly on the server side, and for client input.
 *
 * @details
 * This header declares several structures inheriting from `qb::Event`. These events
 * facilitate typed, asynchronous message passing within the QB actor system.
 *
 * Defined Events:
 * - `NewSessionEvent`: Sent by `AcceptActor` to a `ServerActor` when a new client TCP
 *   connection is accepted. It carries the `qb::io::tcp::socket` for the new session.
 * - `AuthEvent`: Sent by a `ServerActor` (on behalf of a `ChatSession`) to the
 *   `ChatRoomActor` to request user authentication. Contains session ID and username.
 * - `ChatEvent`: Sent by a `ServerActor` (on behalf of a `ChatSession`) to the
 *   `ChatRoomActor` when a client sends a chat message. Contains session ID and message content.
 * - `SendMessageEvent`: Sent by `ChatRoomActor` to a specific `ServerActor` to deliver
 *   a `chat::Message` (from `Protocol.h`) to a client connected to that `ServerActor`.
 *   Contains the target session ID and a `std::shared_ptr` to the `chat::Message` — see the
 *   note on `SendMessageEvent` below for why the message is boxed rather than held by value.
 * - `DisconnectEvent`: Sent by a `ServerActor` (notified by a `ChatSession`) to the
 *   `ChatRoomActor` when a client disconnects. Contains the session ID of the disconnected client.
 * - `ChatInputEvent`: Sent by the client-side `InputActor` to its `ClientActor` when the
 *   user enters a line of text in the console. Contains the user's message.
 *
 * QB Features Demonstrated:
 * - Custom Event Creation: Defining structs inheriting from `qb::Event`.
 * - Data Encapsulation in Events: Events carry necessary data (e.g., sockets, IDs, strings).
 * - `qb::string<N>`: Fixed-size strings for event fields, potentially offering
 *   performance benefits by avoiding heap allocations for small strings when an event is created on the stack.
 * - `std::shared_ptr<const T>`: The other sanctioned payload shape, for data with no useful bound.
 * - `qb::uuid`: For unique session identification.
 * - `qb::io::tcp::socket`: Carried by an event to transfer socket ownership. Relocatable: the
 *   class holds a file-descriptor integer, not a pointer into itself.
 *
 * @note EVENT PAYLOADS MUST BE TRIVIALLY RELOCATABLE. The engine moves an event with `memcpy`
 *       and never runs the source destructor, so a payload member may hold no pointer into
 *       itself. On libstdc++ a SHORT `std::string` holds exactly that — `_M_p` addresses its own
 *       inline SSO buffer — so after the move it still points at the sender's vacated storage.
 *       libc++ recomputes the pointer from `this`, which is why such a defect is structurally
 *       invisible on macOS and corrupts on Linux. This is NOT a cross-core-only concern: pipe
 *       growth, compaction, `reply()` and `forward()` relocate same-core events too. Bounded
 *       payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
 *       `std::vector`, `std::unique_ptr`, `std::shared_ptr` and PODs are safe; the authority is
 *       `qb/tests/core/system/messaging/relocatable-payload.cpp`.
 */

#pragma once

#include <memory>
#include <qb/event.h>
#include <qb/io/tcp/socket.h>
#include <qb/string.h>
#include "Protocol.h"

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
    qb::io::tcp::socket socket; ///< Connected client socket with RAII management
};

/**
 * @brief Event for user authentication requests
 *
 * Flow:
 * 1. Client sends AUTH_REQUEST message
 * 2. ServerActor creates AuthEvent
 * 3. ChatRoomActor processes authentication:
 *    - Validates username availability
 *    - Registers user if allowed
 *    - Sends AUTH_RESPONSE
 *
 * The event source (evt.getSource()) contains the ServerActor's ID,
 * enabling ChatRoomActor to route responses back to the correct client.
 */
struct AuthEvent : public qb::Event {
    qb::uuid       session_id; ///< Unique session identifier
    qb::string<32> username;   ///< Requested username (max 32 chars)
};

/**
 * @brief Event for chat message distribution
 *
 * Flow:
 * 1. Client sends CHAT_MESSAGE
 * 2. ServerActor creates ChatEvent
 * 3. ChatRoomActor processes message:
 *    - Looks up sender's username
 *    - Formats message with sender info
 *    - Broadcasts to all connected clients
 *
 * Messages are limited to 256 characters to prevent abuse
 * and ensure efficient processing.
 */
struct ChatEvent : public qb::Event {
    qb::uuid        session_id; ///< Message sender's session ID
    qb::string<256> message;    ///< Message content (max 256 chars)
};

/**
 * @brief Event for targeted message delivery
 *
 * Flow:
 * 1. ChatRoomActor creates SendMessageEvent
 * 2. Routes to specific ServerActor
 * 3. ServerActor sends through client's socket
 *
 * This event enables direct message delivery to specific
 * clients while maintaining actor system boundaries.
 *
 * WHY THE MESSAGE IS BOXED. `ChatRoomActor` lives on core 3 and every `ServerActor` on core 1
 * (`server/main.cpp`), so this event is relocated with `memcpy` on every delivery. A
 * `chat::Message` held BY VALUE puts its `std::string payload` inside the event, and a short
 * payload — every "Alice: hi", every "Welcome Bob" — lives in that string's inline SSO buffer on
 * libstdc++, addressed by a pointer stored next to it. After the relocation that pointer still
 * addresses the sender's pipe slot, which has since been rewound and reused: `ServerActor` then
 * serialises freed bytes and `~basic_string()` calls `operator delete` on a pointer that never
 * came from the heap. `std::shared_ptr` moves the characters to the heap and leaves nothing in
 * the event that points at itself. It also costs one allocation per BROADCAST rather than one
 * copy per recipient. `const` because the pointee is published to another core: the pointer is
 * what becomes relocatable, not the object it addresses — never mutate a payload after sharing it.
 */
struct SendMessageEvent : public qb::Event {
    qb::uuid                             session_id; ///< Target client's session ID
    std::shared_ptr<const chat::Message> message;    ///< Protocol message to deliver (heap-owned)
};

/**
 * @brief Event for client disconnection handling
 *
 * Flow:
 * 1. Client disconnects or timeout occurs
 * 2. ServerActor detects disconnection
 * 3. ChatRoomActor processes cleanup:
 *    - Removes from session tracking
 *    - Frees username
 *    - Notifies other clients
 *
 * The event source (evt.getSource()) identifies the ServerActor
 * that was handling the disconnected client.
 */
struct DisconnectEvent : public qb::Event {
    qb::uuid session_id; ///< ID of the disconnected session
};

/**
 * @brief Event for client-side user input handling
 *
 * Flow:
 * 1. User enters message in client
 * 2. InputActor creates ChatInputEvent
 * 3. ClientActor processes input:
 *    - Formats as protocol message
 *    - Sends to server if connected
 *
 * This event separates input handling from network I/O,
 * enabling clean separation of concerns in the client.
 */
struct ChatInputEvent : public qb::Event {
    qb::string<256> message; ///< User input message (max 256 chars)
};