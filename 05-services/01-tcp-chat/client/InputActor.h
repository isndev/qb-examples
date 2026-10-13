/**
 * @file examples/05-services/01-tcp-chat/client/InputActor.h
 * @example TCP Chat Client - User Input Actor
 * @brief Actor handling console input on a dedicated core.
 *
 * @details
 * This actor uses `qb::ICallback` to invoke `std::getline` on its core. That call
 * blocks the core until a complete line or EOF; it is not asynchronous input.
 * Other nonempty lines are packaged into `ChatInputEvent`s and sent to `ClientActor`;
 * `quit` and empty lines are handled locally.
 *
 * The dedicated core keeps the network actor on another core responsive, but an
 * open stdin without a newline still prevents this core from processing events.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: Base class for concurrent entities.
 * - `qb::ICallback`: Interface for periodic execution within an actor's lifecycle.
 *   - `registerCallback(*this)`: Enables calls to `on(qb::LoopEvent const&)`.
 *   - `on(qb::LoopEvent const&)`: Called by the QB engine each loop pass.
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
 * - Uses QB's callback system, but the console read itself blocks this core
 * - Maintains clean separation from network logic
 * - Implements command parsing and routing
 * - Handles graceful shutdown sequences
 *
 * Input Processing:
 * - Reads complete lines from standard input, blocking until line or EOF
 * - Parses special commands (e.g., 'quit')
 * - Routes chat messages to ClientActor
 * - Manages application lifecycle
 */
class InputActor
    : public qb::Actor
    , public qb::ICallback {
private:
    qb::ActorId _client_id;     ///< Reference to network handler
    bool        _running{true}; ///< Actor lifecycle control

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
    qb::io::async::task<bool> onInit() override;

    /// Default destructor; a blocked input callback must return before the actor can stop.
    ~InputActor() = default;

private:
    /**
     * @brief Processes a complete console line; the read can block this core
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
    void on(qb::LoopEvent const &) override;
};
