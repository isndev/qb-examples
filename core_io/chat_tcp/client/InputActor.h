/**
 * @file InputActor.h
 * @brief User input handling actor for the chat client
 * 
 * This actor manages the user interface aspects of the chat client:
 * - Non-blocking console input handling
 * - Message formatting and routing
 * - Command interpretation
 * - Clean shutdown handling
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