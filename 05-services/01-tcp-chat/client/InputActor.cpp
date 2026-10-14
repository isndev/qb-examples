/**
 * @file examples/05-services/01-tcp-chat/client/InputActor.cpp
 * @example TCP Chat Client - User Input Actor Implementation
 * @brief Implements nonblocking delivery from a cancellable console reader.
 *
 * @details
 * This file contains the implementation of `InputActor`.
 * - `onInit()`: Registers the `qb::ICallback` and displays initial user prompts.
 * - `on(qb::LoopEvent const&)`: Drains ready lines without waiting on stdin.
 *   - "quit", EOF and input errors terminate both client actors with `qb::KillEvent`.
 *   - Otherwise, non-empty input is sent as a `ChatInputEvent` to the `ClientActor`.
 *
 * QB Features Demonstrated (in context of this implementation):
 * - `qb::ICallback` usage for periodic actions.
 * - Sending events (`ChatInputEvent`, `qb::KillEvent`) to other actors and self.
 * - `qb::io::cout()` for console output.
 */

#include "InputActor.h"
#include <iostream>

InputActor::InputActor(qb::ActorId client_id, qb_example::input::LineSource &input)
    : _client_id(client_id)
    , _input(input) {}

qb::io::async::task<bool>
InputActor::onInit() {
    // Every callback returns promptly, even with an open partial input line.
    registerCallback(*this);

    // Display initialization and usage information
    qb::io::cout() << "InputActor initialized with ID: " << id() << std::endl;
    qb::io::cout() << "Enter messages (or 'quit' to exit):" << std::endl;

    co_return true;
}

void
InputActor::on(qb::LoopEvent const &) {
    if (!_running) {
        return;
    }

    qb_example::input::Event next;
    for (std::size_t drained = 0; drained < 16 && _input.try_pop(next); ++drained) {
        if (next.kind != qb_example::input::Event::Kind::line) {
            if (next.kind == qb_example::input::Event::Kind::error) {
                qb::io::cerr() << "Console input failed; closing chat client" << std::endl;
            }
            shutdown();
            return;
        }
        if (next.text == "quit") {
            shutdown();
            return;
        }
        if (next.text.empty()) {
            continue;
        }
        // ChatInputEvent::message is qb::string<256>; never silently truncate input.
        if (next.text.size() > 256) {
            qb::io::cerr() << "Chat message exceeds 256 bytes; line ignored" << std::endl;
            continue;
        }
        auto &evt   = push<ChatInputEvent>(_client_id);
        evt.message = std::move(next.text);
    }
}

void
InputActor::shutdown() {
    _running = false;
    push<qb::KillEvent>(_client_id);
    push<qb::KillEvent>(id());
}
