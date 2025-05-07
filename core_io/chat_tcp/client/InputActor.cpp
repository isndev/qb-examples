/**
 * @file InputActor.cpp
 * @brief Implementation of the user input handling actor
 * 
 * This file contains the implementation of the InputActor class,
 * handling user interaction and command processing for the chat client.
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