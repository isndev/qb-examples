/**
 * @file examples/05-services/01-tcp-chat/client/main.cpp
 * @tier 05-services
 * @teaches The other half of a two-binary project: a cancellable console reader outside
 *          the engine, input delivery on one core and the socket on another, with an exit
 *          code that reports what the engine actually did.
 * @demonstrates qb::Main, addActor<T>, qb::io::uri, qb::ActorId, engine.start, engine.join,
 *               engine.hasError, qb::io::cout, qb::io::cerr
 * @prerequisites 01-actors/04-cores-and-placement, 02-io/03-tcp
 * @expect "Connecting to chat server at "
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
 * 4.  Starts the QB engine asynchronously, joins its cores and stops the console reader.
 * 5.  Includes basic error handling for argument parsing and engine execution.
 *
 * This separates console I/O from both actor cores, so a partial line cannot hold up
 * engine shutdown or network operations.
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
#include <chrono>
#include <functional>
#include <iostream>
#include "../../../common/CancelableLineSource.h"
#include "ClientActor.h"
#include "InputActor.h"
#include "../shared/Events.h"

/**
 * @brief Application entry point
 *
 * Command-line usage:
 * @code
 * qb-example-services-tcp-chat-client <host> <port> <username>
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
int
main(int argc, char *argv[]) {
    if (auto helper = qb_example::input::maybe_run_helper(argc, argv)) {
        return *helper;
    }

    // Validate command-line arguments
    if (argc != 4) {
        qb::io::cerr() << "Usage: " << argv[0] << " <host> <port> <username>" << std::endl;
        return 1;
    }

    try {
        // The source outlives the engine and every actor holding its reference.
        qb_example::input::LineSource input;
        qb::Main                      engine;

        // Prepare server connection URI
        qb::io::uri server_uri("tcp://" + std::string(argv[1]) + ":" + std::string(argv[2]));

        // Create actor system with proper core distribution
        auto client_id = qb::ActorId(); // Placeholder for client ID

        // Create InputActor on core 0 for UI handling
        auto input_id = engine.addActor<InputActor>(0,                   // Core 0: User interface
                                                    std::ref(client_id), // Reference for message routing
                                                    std::ref(input)      // Cancellable console source
        );

        // Create ClientActor on core 1 for network handling
        client_id = engine.addActor<ClientActor>(1,                    // Core 1: Network operations
                                                 std::string(argv[3]), // Username
                                                 input_id,             // Input actor reference
                                                 server_uri            // Server address
        );

        // The input core has no socket wakeup; a short park avoids idle polling at 100% CPU.
        engine.core(0).setLatency(std::chrono::milliseconds{5});

        // Start the engine and wait for completion
        qb::io::cout() << "Connecting to chat server at " << server_uri.source() << std::endl;
        engine.start();
        engine.join();
        input.stop();

        // Return status based on engine state
        return engine.hasError() ? 1 : 0;

    } catch (const std::exception &e) {
        // Handle initialization and runtime errors
        qb::io::cerr() << "Error: " << e.what() << std::endl;
        return 1;
    }
}
