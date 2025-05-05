/**
 * @file main.cpp
 * @brief Chat client application entry point
 * 
 * This file implements the chat client's core architecture:
 * 
 * System Architecture:
 * - Multi-core actor distribution
 * - Clean separation of concerns
 * - Robust error handling
 * - Resource management
 * 
 * Actor Distribution:
 * - InputActor (Core 0): User interface and input handling
 * - ClientActor (Core 1): Network communication and protocol
 * 
 * The separation across cores enables:
 * - Non-blocking user interface
 * - Responsive network handling
 * - Optimal resource utilization
 */

#include <qb/main.h>
#include <qb/io/uri.h>
#include <iostream>
#include "ClientActor.h"
#include "InputActor.h"
#include "../shared/Events.h"

/**
 * @brief Application entry point
 * 
 * Command-line usage:
 * @code
 * chat_client <host> <port> <username>
 * @endcode
 * 
 * Initialization flow:
 * 1. Validates command-line arguments
 * 2. Creates QB engine instance
 * 3. Sets up actor system:
 *    - InputActor on core 0
 *    - ClientActor on core 1
 * 4. Starts engine and waits for completion
 * 
 * @param argc Number of command-line arguments
 * @param argv Command-line arguments array
 * @return 0 on success, 1 on error
 */
int main(int argc, char* argv[]) {
    // Validate command-line arguments
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> <username>" << std::endl;
        return 1;
    }

    try {
        // Initialize QB engine
        qb::Main engine;

        // Prepare server connection URI
        qb::io::uri server_uri("tcp://" + std::string(argv[1]) + ":" + std::string(argv[2]));

        // Create actor system with proper core distribution
        auto client_id = qb::ActorId();  // Placeholder for client ID
        
        // Create InputActor on core 0 for UI handling
        auto input_id = engine.addActor<InputActor>(
            0,              // Core 0: User interface
            std::ref(client_id)  // Reference for message routing
        );

        // Create ClientActor on core 1 for network handling
        client_id = engine.addActor<ClientActor>(
            1,                    // Core 1: Network operations
            std::string(argv[3]), // Username
            input_id,             // Input actor reference
            server_uri           // Server address
        );

        // Start the engine and wait for completion
        std::cout << "Connecting to chat server at " << server_uri.source() << std::endl;
        engine.start();
        engine.join();

        // Return status based on engine state
        return engine.hasError() ? 1 : 0;

    } catch (const std::exception& e) {
        // Handle initialization and runtime errors
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 