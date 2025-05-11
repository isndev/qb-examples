/**
 * @file examples/core_io/chat_tcp/client/InputActor.h
 * @example TCP Chat Client - User Input Actor
 * @brief Actor responsible for handling user console input in a non-blocking manner.
 *
 * @details
 * This actor interfaces with the user via the console. It uses `qb::ICallback` to
 * periodically check for console input without blocking the actor's thread or the
 * QB engine's event loop. When input is detected, it's packaged into a
 * `ChatInputEvent` and sent to the `ClientActor` for processing and network transmission.
 *
 * It demonstrates a common pattern for integrating blocking I/O (like `std::getline`
 * from `std::cin` in a naive way) into the QB actor model by using a non-blocking check
 * within a periodic callback.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: Base class for concurrent entities.
 * - `qb::ICallback`: Interface for periodic execution within an actor's lifecycle.
 *   - `registerCallback(*this)`: To enable periodic calls to `onCallback()`.
 *   - `onCallback()`: Method called by the QB engine periodically.
 * - `qb::Event`: Base for `ChatInputEvent`.
 * - Inter-Actor Communication: `push<ChatInputEvent>(_client_id, ...)` to send user input to the `ClientActor`.
 * - Actor Lifecycle: Handles `qb::KillEvent` implicitly for shutdown (though explicit handling could be added).
 */

#pragma once

#include <qb/actor.h>
#include <qb/icallback.h>
#include <string>
#include "ClientActor.h"
#include "../shared/Events.h"

/**
 * @brief Actor managing user input and command processing
 * 
 * Architecture:
 * - Uses QB's callback system for non-blocking I/O
 * - Maintains clean separation from network logic
 * - Implements command parsing and routing
 * - Handles graceful shutdown sequences
 * 
 * Input Processing:
 * - Reads from standard input non-blockingly
 * - Parses special commands (e.g., 'quit')
 * - Routes chat messages to ClientActor
 * - Manages application lifecycle
 */
class InputActor : public qb::Actor,
                  public qb::ICallback {
private:
    qb::ActorId _client_id;    ///< Reference to network handler
    bool _running{true};        ///< Actor lifecycle control

public:
    /**
     * @brief Constructs input handling actor
     * 
     * @param client_id Reference to the client actor for message routing
     */
    explicit InputActor(qb::ActorId client_id);

    /**
     * @brief Initializes input handling system
     * 
     * Setup sequence:
     * 1. Registers callback handler
     * 2. Displays user instructions
     * 3. Prepares input processing
     * 
     * @return true if initialization succeeds
     */
    bool onInit() override;

    /// Ensures clean shutdown
    ~InputActor() = default;

private:
    /**
     * @brief Processes user input non-blockingly
     * 
     * Input handling:
     * 1. Reads console input
     * 2. Processes commands:
     *    - 'quit': Initiates shutdown
     *    - Other: Routes as chat message
     * 3. Manages actor lifecycle
     * 
     * Command flow:
     * - Empty lines are ignored
     * - 'quit' triggers clean shutdown
     * - Other input creates ChatInputEvent
     */
    void onCallback() override;
}; 