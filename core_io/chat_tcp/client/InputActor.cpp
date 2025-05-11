/**
 * @file examples/core_io/chat_tcp/client/InputActor.cpp
 * @example TCP Chat Client - User Input Actor Implementation
 * @brief Implements the `InputActor` for handling non-blocking console input.
 *
 * @details
 * This file contains the implementation of `InputActor`.
 * - `onInit()`: Registers the `qb::ICallback` and displays initial user prompts.
 * - `onCallback()`: This method is called periodically by the QB engine. It attempts
 *   to read a line from `std::cin`. If input is available (e.g., user pressed Enter),
 *   it processes the input. The `std::getline` itself can be blocking if input isn't ready,
 *   so for a truly non-blocking GUI or more complex console input, platform-specific
 *   non-blocking I/O or a separate input thread that messages the actor would be more robust.
 *   However, for simple console examples, `ICallback` with `std::getline` is often used for simplicity.
 *   - If the input is "quit", it signals the `ClientActor` and itself to terminate using `qb::KillEvent`.
 *   - Otherwise, non-empty input is sent as a `ChatInputEvent` to the `ClientActor`.
 *
 * QB Features Demonstrated (in context of this implementation):
 * - `qb::ICallback` usage for periodic actions.
 * - Sending events (`ChatInputEvent`, `qb::KillEvent`) to other actors and self.
 * - `qb::io::cout()` for console output.
 */

#include "InputActor.h"
#include <iostream>

InputActor::InputActor(qb::ActorId client_id)
    : _client_id(client_id) {}

bool InputActor::onInit() {
    // Register for non-blocking input handling
    registerCallback(*this);
    
    // Display initialization and usage information
    qb::io::cout() << "InputActor initialized with ID: " << id() << std::endl;
    qb::io::cout() << "Enter messages (or 'quit' to exit):" << std::endl;
    
    return true;
}

void InputActor::onCallback() {
    std::string line;

    // Read user input non-blockingly
    std::getline(std::cin, line);
    
    // Handle quit command
    if (line == "quit") {
        _running = false;
        push<qb::KillEvent>(_client_id);  // Signal client to shutdown
        push<qb::KillEvent>(id());        // Schedule own termination
        return;
    }

    // Process non-empty input as chat message
    if (!line.empty()) {
        auto& evt = push<ChatInputEvent>(_client_id);
        evt.message = std::move(line);
    }
} 