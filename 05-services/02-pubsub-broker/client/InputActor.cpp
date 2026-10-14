/**
 * @file examples/05-services/02-pubsub-broker/client/InputActor.cpp
 * @example Message Broker Client - User Input Actor Implementation
 * @brief Implements the `InputActor` for handling console input for the broker client.
 *
 * @details
 * This file provides the `InputActor` implementation.
 * - `onInit()`: Registers the `qb::ICallback` and displays a welcome message and help info.
 * - `on(qb::LoopEvent const&)`: Drains ready lines without waiting on stdin.
 *   Handles "quit", EOF and input errors by sending `qb::KillEvent` to both actors.
 *   Handles "help" to redisplay command usage.
 *   Other non-empty input is sent as a `BrokerInputEvent` to the `ClientActor`.
 * - `displayHelp()`: Prints available commands and usage examples to the console.
 *
 * QB Features Demonstrated (in context of this implementation):
 * - `qb::ICallback` for periodic input polling.
 * - Sending events (`BrokerInputEvent`, `qb::KillEvent`).
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
    qb::io::cout() << "Welcome to the QB Message Broker client" << std::endl;
    displayHelp();

    co_return true;
}

void
InputActor::on(qb::LoopEvent const & /*loop*/) {
    if (!_running) {
        return;
    }

    qb_example::input::Event next;
    for (std::size_t drained = 0; drained < 16 && _input.try_pop(next); ++drained) {
        if (next.kind != qb_example::input::Event::Kind::line) {
            if (next.kind == qb_example::input::Event::Kind::error) {
                qb::io::cerr() << "Console input failed; closing broker client" << std::endl;
            }
            shutdown();
            return;
        }
        if (next.text == "quit") {
            shutdown();
            return;
        }
        if (next.text == "help") {
            displayHelp();
            continue;
        }
        if (next.text.empty()) {
            continue;
        }
        // BrokerInputEvent::command is qb::string<1024>; never silently truncate input.
        if (next.text.size() > 1024) {
            qb::io::cerr() << "Broker command exceeds 1024 bytes; line ignored" << std::endl;
            continue;
        }
        auto &evt   = push<BrokerInputEvent>(_client_id);
        evt.command = std::move(next.text);
    }
}

void
InputActor::shutdown() {
    _running = false;
    push<qb::KillEvent>(_client_id);
    push<qb::KillEvent>(id());
}

void
InputActor::displayHelp() {
    qb::io::cout() << "\nAvailable commands:" << std::endl;
    qb::io::cout() << "  SUB <topic>            - Subscribe to a topic" << std::endl;
    qb::io::cout() << "  UNSUB <topic>          - Unsubscribe from a topic" << std::endl;
    qb::io::cout() << "  PUB <topic> <message>  - Publish a message to a topic" << std::endl;
    qb::io::cout() << "  help                   - Display this help message" << std::endl;
    qb::io::cout() << "  quit                   - Exit the client" << std::endl;
    qb::io::cout() << "\nExamples:" << std::endl;
    qb::io::cout() << "  SUB news               - Subscribe to the 'news' topic" << std::endl;
    qb::io::cout() << "  PUB news Hello World   - Publish 'Hello World' to the 'news' topic" << std::endl;
    qb::io::cout() << "  UNSUB news             - Unsubscribe from the 'news' topic" << std::endl;
    qb::io::cout() << "\nEnter commands below:" << std::endl;
}
