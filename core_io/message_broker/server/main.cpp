/**
 * @file examples/core_io/message_broker/server/main.cpp
 * @example Message Broker Server - Application Entry Point
 * @brief Main entry point for the message broker server application.
 *
 * @details
 * This file sets up and launches the server-side actor system for the message broker.
 * It demonstrates a multi-core deployment strategy:
 * 1.  Initializes the `qb::Main` engine.
 * 2.  Creates the `TopicManagerActor` on a dedicated core (core 2), which handles the
 *     central logic of topic management and message routing.
 * 3.  Creates a pool of `ServerActor`s on another core (core 1). These actors manage
 *     client I/O sessions and interface with the `TopicManagerActor`.
 * 4.  Creates an `AcceptActor` on a third core (core 0). This actor listens for
 *     incoming client connections on a specific port (12345) and distributes them
 *     to the `ServerActor` pool.
 * 5.  Starts the QB engine asynchronously and waits for user input (Enter key) to initiate
 *     a graceful shutdown (`engine.stop()`, `engine.join()`).
 *
 * This architecture separates concerns: connection acceptance, session/IO handling,
 * and core application logic are handled by different sets of actors on different cores.
 *
 * QB Features Demonstrated:
 * - `qb::Main`: Actor system engine.
 * - `engine.addActor<ActorType>(core_id, args...)`: Multi-core actor deployment.
 * - `engine.core(core_id).builder()`: Fluent API for actor creation on a core.
 * - `qb::ActorIdList`: For passing `ServerActor` IDs to `AcceptActor`.
 * - Asynchronous Engine Start & Graceful Shutdown.
 * - `qb::io::uri`: For specifying listen address.
 */

#include <qb/main.h>
#include "AcceptActor.h"
#include "ServerActor.h"
#include "TopicManagerActor.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Create the QB engine instance
    qb::Main engine;

    try {
        // Step 1: Create TopicManagerActor (Core 2)
        // - Central component for state management
        // - Placed on separate core to handle broadcasts
        auto topic_manager_id = engine.addActor<TopicManagerActor>(2);

        // Step 2: Create ServerActors (Core 1)
        // - Multiple instances for connection handling
        // - Share topic_manager_id for message routing
        // - Builder pattern for multiple actors
        auto server_ids = engine.core(1).builder()
            .addActor<ServerActor>(topic_manager_id)
            .addActor<ServerActor>(topic_manager_id)
            .idList();

        // Step 3: Create AcceptActor (Core 0)
        // - Single listener on port 12345
        // - Shares server_ids for connection distribution
        // - Demonstrate QB's URI-based configuration
        engine.core(0).builder()
            .addActor<AcceptActor>(qb::io::uri{"tcp://0.0.0.0:12345"}, server_ids);

        // Step 4: Log system configuration
        // - Display actor IDs for debugging
        // - Show listening port
        // - Confirm initialization
        qb::io::cout() << "Message broker server started on port 12345" << std::endl;
        qb::io::cout() << "TopicManager ID: " << topic_manager_id << std::endl;
        qb::io::cout() << "Server1 ID: " << server_ids[0] << std::endl;
        qb::io::cout() << "Server2 ID: " << server_ids[1] << std::endl;

        // Step 5: Start the engine
        // - Asynchronous start for interactive shutdown
        // - Wait for user input to stop
        // - Demonstrate proper shutdown sequence
        engine.start(true);  // asynchronous start
        qb::io::cout() << "Engine is running" << std::endl;
        qb::io::cout() << "Press Enter to stop the engine" << std::endl;
        std::cin.get();
        
        // Step 6: Clean shutdown
        // - Stop engine gracefully
        // - Wait for all actors to finish
        // - Ensure proper cleanup
        engine.stop();
        engine.join();
    }
    catch (const std::exception& e) {
        qb::io::cerr() << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 