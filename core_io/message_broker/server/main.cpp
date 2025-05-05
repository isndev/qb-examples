/**
 * @file main.cpp
 * @brief Entry point demonstrating QB system architecture and initialization
 * 
 * This file demonstrates:
 * 1. How to set up a multi-core QB application
 * 2. How to create and configure different types of actors
 * 3. How to manage actor dependencies and relationships
 * 4. How to handle system startup and shutdown
 * 
 * System Architecture:
 * - Core 0: AcceptActor (Connection acceptance)
 *   - Single instance for connection acceptance
 *   - Listening on port 12345
 * 
 * - Core 1: ServerActors (Session management)
 *   - Two instances for scalability
 *   - Handle client sessions
 *   - Route messages to TopicManager
 * 
 * - Core 2: TopicManagerActor (Central state)
 *   - Single instance for consistency
 *   - Manages topic subscriptions
 *   - Handles message routing
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
        std::cout << "Message broker server started on port 12345" << std::endl;
        std::cout << "TopicManager ID: " << topic_manager_id << std::endl;
        std::cout << "Server1 ID: " << server_ids[0] << std::endl;
        std::cout << "Server2 ID: " << server_ids[1] << std::endl;

        // Step 5: Start the engine
        // - Asynchronous start for interactive shutdown
        // - Wait for user input to stop
        // - Demonstrate proper shutdown sequence
        engine.start(true);  // asynchronous start
        std::cout << "Engine is running" << std::endl;
        std::cout << "Press Enter to stop the engine" << std::endl;
        std::cin.get();
        
        // Step 6: Clean shutdown
        // - Stop engine gracefully
        // - Wait for all actors to finish
        // - Ensure proper cleanup
        engine.stop();
        engine.join();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 