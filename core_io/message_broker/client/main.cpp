/**
 * @file examples/core_io/message_broker/client/main.cpp
 * @example Message Broker Client - Application Entry Point
 * @brief Main entry point for the message broker client application.
 *
 * @details
 * Sets up and launches the client-side actor system for the message broker.
 * 1.  Parses command-line arguments: server host and port.
 * 2.  Initializes the `qb::Main` engine.
 * 3.  Creates actors with core assignments:
 *     -   `InputActor` (core 0): Handles console input.
 *     -   `ClientActor` (core 1): Manages network communication with the broker server.
 * 4.  Starts the QB engine (`engine.start()`) and waits for termination (`engine.join()`).
 * This structure separates UI input from network logic for better responsiveness.
 *
 * QB Features Demonstrated:
 * - `qb::Main`: Actor system engine.
 * - `engine.addActor<ActorType>(core_id, args...)`: Multi-core actor deployment.
 * - `engine.start(true)`, `engine.join()`: Engine lifecycle.
 * - `qb::io::uri`: For server address representation.
 * - `qb::ActorId`: For inter-actor referencing.
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
 * broker_client <host> <port>
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
    if (argc != 3) {
        qb::io::cerr() << "Usage: " << argv[0] << " <host> <port>" << std::endl;
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
            input_id,             // Input actor reference
            server_uri           // Server address
        );

        // Start the engine and wait for completion
        qb::io::cout() << "Connecting to message broker at " << server_uri.source() << std::endl;
        engine.start();
        engine.join();

        // Return status based on engine state
        return engine.hasError() ? 1 : 0;

    } catch (const std::exception& e) {
        // Handle initialization and runtime errors
        qb::io::cerr() << "Error: " << e.what() << std::endl;
        return 1;
    }
} 