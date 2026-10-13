/**
 * @file examples/05-services/01-tcp-chat/client/InputActor.cpp
 * @example TCP Chat Client - User Input Actor Implementation
 * @brief Implements the `InputActor` with a blocking read on its dedicated core.
 *
 * @details
 * This file contains the implementation of `InputActor`.
 * - `onInit()`: Registers the `qb::ICallback` and displays initial user prompts.
 * - `on(qb::LoopEvent const&)`: Reads a line from `std::cin` with `std::getline`.
 *   An open stdin without a complete line blocks this core; EOF is not yet a
 *   terminal path here. The network actor runs on another core.
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

qb::io::async::task<bool>
InputActor::onInit() {
    // Register a loop callback; getline below still blocks this dedicated core.
    registerCallback(*this);

    // Display initialization and usage information
    qb::io::cout() << "InputActor initialized with ID: " << id() << std::endl;
    qb::io::cout() << "Enter messages (or 'quit' to exit):" << std::endl;

    co_return true;
}

void
InputActor::on(qb::LoopEvent const &) {
    std::string line;

    // Wait for a whole line. An open stdin without newline blocks this core.
    std::getline(std::cin, line);

    // Handle quit command
    if (line == "quit") {
        _running = false;
        push<qb::KillEvent>(_client_id); // Signal client to shutdown
        push<qb::KillEvent>(id());       // Schedule own termination
        return;
    }

    // Process non-empty input as chat message
    if (!line.empty()) {
        auto &evt   = push<ChatInputEvent>(_client_id);
        evt.message = std::move(line);
    }
}
