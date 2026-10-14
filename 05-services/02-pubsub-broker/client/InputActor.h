/**
 * @file examples/05-services/02-pubsub-broker/client/InputActor.h
 * @example Message Broker Client - User Input Actor
 * @brief Actor for handling user console input for the message broker client.
 *
 * @details
 * This actor captures user commands from the console for interacting with the message broker.
 * It uses `qb::ICallback` to drain a cancellable reader outside the engine;
 * the callback never waits for a complete line.
 * Entered commands are packaged into `BrokerInputEvent`s and sent to the `ClientActor`
 * for parsing and network transmission.
 * "quit" and "help" are handled locally; EOF and input errors end both actors.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: Base class.
 * - `qb::ICallback`: For a short, nonblocking per-loop callback.
 *   - `registerCallback(*this)` and `on(qb::LoopEvent const&)`.
 * - `qb::Event`: Base for `BrokerInputEvent`.
 * - Inter-Actor Communication: `push<BrokerInputEvent>(...)` to `ClientActor`.
 * - Local Command Handling: Processing "quit" and "help" directly.
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
 * - Routes broker commands to ClientActor
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
     *    - 'help': Displays usage help
     *    - Other: Routes as broker command
     * 3. Manages actor lifecycle
     *
     * Command flow:
     * - Empty lines are ignored
     * - 'quit', EOF and input errors trigger clean shutdown
     * - Other input creates BrokerInputEvent
     */
    void on(qb::LoopEvent const &loop) override;

    void shutdown();

    /**
     * @brief Displays help information
     *
     * Shows usage instructions:
     * - Available commands
     * - Command syntax
     * - Examples
     */
    void displayHelp();
};
