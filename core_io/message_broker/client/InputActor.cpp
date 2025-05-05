/**
 * @file InputActor.cpp
 * @brief Implementation of the user input handling actor
 * 
 * This file contains the implementation of the InputActor class,
 * handling user interaction and command processing for the broker client.
 */

#include "InputActor.h"
#include <iostream>

InputActor::InputActor(qb::ActorId client_id)
    : _client_id(client_id) {}

bool InputActor::onInit() {
    // Register for non-blocking input handling
    registerCallback(*this);
    
    // Display initialization and usage information
    std::cout << "InputActor initialized with ID: " << id() << std::endl;
    std::cout << "Welcome to the QB Message Broker client" << std::endl;
    displayHelp();
    
    return true;
}

void InputActor::onCallback() {
    std::string line;

    // Read user input non-blockingly
    std::getline(std::cin, line);
    
    // Handle special commands
    if (line == "quit") {
        _running = false;
        push<qb::KillEvent>(_client_id);  // Signal client to shutdown
        push<qb::KillEvent>(id());        // Schedule own termination
        return;
    }
    
    if (line == "help") {
        displayHelp();
        return;
    }

    // Process non-empty input as broker command
    if (!line.empty()) {
        auto& evt = push<BrokerInputEvent>(_client_id);
        evt.command = std::move(line);
    }
}

void InputActor::displayHelp() {
    std::cout << "\nAvailable commands:" << std::endl;
    std::cout << "  SUB <topic>            - Subscribe to a topic" << std::endl;
    std::cout << "  UNSUB <topic>          - Unsubscribe from a topic" << std::endl;
    std::cout << "  PUB <topic> <message>  - Publish a message to a topic" << std::endl;
    std::cout << "  help                   - Display this help message" << std::endl;
    std::cout << "  quit                   - Exit the client" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  SUB news               - Subscribe to the 'news' topic" << std::endl;
    std::cout << "  PUB news Hello World   - Publish 'Hello World' to the 'news' topic" << std::endl;
    std::cout << "  UNSUB news             - Unsubscribe from the 'news' topic" << std::endl;
    std::cout << "\nEnter commands below:" << std::endl;
} 