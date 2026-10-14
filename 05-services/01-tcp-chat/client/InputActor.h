/**
 * @file examples/05-services/01-tcp-chat/client/InputActor.h
 * @example TCP Chat Client - User Input Actor
 * @brief Actor handling console input on a dedicated core.
 *
 * @details
 * A cancellable reader outside the engine owns stdin. This actor uses `qb::ICallback`
 * to drain ready lines without waiting on input. Nonempty lines become
 * `ChatInputEvent`s; `quit`, EOF and input errors end both client actors.
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
#include "../../../common/CancelableLineSource.h"
#include "ClientActor.h"
#include "../shared/Events.h"

/**
 * @brief Actor managing user input and command processing
 *
 * Architecture:
 * - Uses QB's callback system to drain a bounded number of ready lines per pass
 * - Maintains clean separation from network logic
 * - Implements command parsing and routing
 * - Handles graceful shutdown sequences
 *
 * Input Processing:
 * - Reads complete lines from the cancellable source without blocking the core
 * - Parses special commands (e.g., 'quit')
 * - Routes chat messages to ClientActor
 * - Manages application lifecycle
 */
class InputActor
    : public qb::Actor
    , public qb::ICallback {
private:
    qb::ActorId                    _client_id;     ///< Reference to network handler
    qb_example::input::LineSource &_input;         ///< Owned by main, outlives the engine
    bool                           _running{true}; ///< Actor lifecycle control

public:
    /**
     * @brief Constructs input handling actor
     *
     * @param client_id Reference to the client actor for message routing
     * @param input Cancellable source owned by main
     */
    InputActor(qb::ActorId client_id, qb_example::input::LineSource &input);

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

    /// Default destructor; callback never waits for console input.
    ~InputActor() = default;

private:
    /**
     * @brief Drains at most sixteen ready console lines without blocking this core
     *
     * Input handling:
     * 1. Tries to pop ready console input
     * 2. Processes commands:
     *    - 'quit': Initiates shutdown
     *    - Other: Routes as chat message
     * 3. Manages actor lifecycle
     *
     * Command flow:
     * - Empty lines are ignored
     * - 'quit', EOF and input errors trigger clean shutdown
     * - Other input creates ChatInputEvent
     */
    void on(qb::LoopEvent const &) override;

    void shutdown();
};
