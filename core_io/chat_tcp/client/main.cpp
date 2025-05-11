/**
 * @file examples/core_io/chat_tcp/client/main.cpp
 * @example TCP Chat Client - Application Entry Point
 * @brief Main entry point for the TCP chat client application.
 *
 * @details
 * This file sets up and launches the client-side actor system for the TCP chat application.
 * It performs the following steps:
 * 1.  Parses command-line arguments for server host, port, and username.
 * 2.  Initializes the `qb::Main` engine.
 * 3.  Creates and configures the necessary actors:
 *     -   `InputActor`: Placed on core 0, responsible for handling user console input.
 *         It takes a reference to the `ClientActor`'s ID to send `ChatInputEvent`s.
 *     -   `ClientActor`: Placed on core 1, responsible for network communication with the
 *         chat server. It takes the username, the `InputActor`'s ID (to know its counterpart,
 *         though not strictly used for sending messages back in this example), and the server URI.
 * 4.  Starts the QB engine using `engine.start()` (asynchronously) and then waits for the
 *     engine to terminate using `engine.join()`.
 * 5.  Includes basic error handling for argument parsing and engine execution.
 *
 * This demonstrates a simple multi-core actor deployment strategy where UI/input handling
 * is separated from network operations, promoting responsiveness.
 *
 * QB Features Demonstrated:
 * - `qb::Main`: The main engine kvinnherad for the actor system.
 * - `engine.addActor<ActorType>(core_id, args...)`: For creating actors and assigning them to specific cores.
 * - `engine.start(true)`: To start the engine asynchronously.
 * - `engine.join()`: To wait for the engine and all its actors to terminate.
 * - `engine.hasError()`: To check for errors during engine execution.
 * - `qb::io::uri`: For parsing and representing the server connection URI.
 * - `qb::ActorId`: Used for inter-actor referencing (passed during construction).
 * - Basic multi-core actor deployment.
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
        qb::io::cerr() << "Usage: " << argv[0] << " <host> <port> <username>" << std::endl;
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
        qb::io::cout() << "Connecting to chat server at " << server_uri.source() << std::endl;
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