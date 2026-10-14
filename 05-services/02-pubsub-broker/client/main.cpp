/**
 * @file examples/05-services/02-pubsub-broker/client/main.cpp
 * @tier 05-services
 * @teaches A publish/subscribe client: the same input-actor / network-actor split as the chat
 *          client, driving subscribe, unsubscribe and publish over one connection.
 * @demonstrates qb::Main, addActor<T>, qb::io::uri, qb::ActorId, engine.start, engine.join,
 *               engine.hasError, qb::io::cout, qb::io::cerr
 * @prerequisites 05-services/01-tcp-chat
 * @expect "Connecting to message broker at "
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
 * 4.  Starts the QB engine, joins its cores and stops the cancellable console reader.
 * Console I/O stays outside both actor cores, including during partial-line input.
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
 * qb-example-services-pubsub-broker-client <host> <port>
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
    if (argc != 3) {
        qb::io::cerr() << "Usage: " << argv[0] << " <host> <port>" << std::endl;
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
        client_id = engine.addActor<ClientActor>(1,         // Core 1: Network operations
                                                 input_id,  // Input actor reference
                                                 server_uri // Server address
        );

        // The input core has no socket wakeup; a short park avoids idle polling at 100% CPU.
        engine.core(0).setLatency(std::chrono::milliseconds{5});

        // Start the engine and wait for completion
        qb::io::cout() << "Connecting to message broker at " << server_uri.source() << std::endl;
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
