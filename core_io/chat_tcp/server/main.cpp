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
 * - Core 0: AcceptActors (Connection acceptance)
 *   - Two instances for load distribution
 *   - Each listening on different ports
 * 
 * - Core 1: ServerActors (Session management)
 *   - Two instances for scalability
 *   - Handle client sessions
 *   - Route messages to ChatRoom
 * 
 * - Core 3: ChatRoomActor (Central state)
 *   - Single instance for consistency
 *   - Manages global chat state
 *   - Handles message broadcasting
 */

#include <qb/main.h>
#include "AcceptActor.h"
#include "ServerActor.h"
#include "ChatRoomActor.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Create the QB engine instance
    qb::Main engine;

    try {
        // Step 1: Create ChatRoomActor (Core 3)
        // - Central component for state management
        // - Placed on separate core to handle broadcasts
        auto chatroom_id = engine.addActor<ChatRoomActor>(3);

        // Step 2: Create ServerActors (Core 1)
        // - Multiple instances for connection handling
        // - Share chatroom_id for message routing
        // - Builder pattern for multiple actors
        auto server_ids = engine.core(1).builder()
            .addActor<ServerActor>(chatroom_id)
            .addActor<ServerActor>(chatroom_id)
            .idList();

        // Step 3: Create AcceptActors (Core 0)
        // - Multiple listeners on different ports
        // - Share server_ids for connection distribution
        // - Demonstrate QB's URI-based configuration
        engine.core(0).builder()
            .addActor<AcceptActor>(qb::io::uri{"tcp://0.0.0.0:3001"}, server_ids)
            .addActor<AcceptActor>(qb::io::uri{"tcp://0.0.0.0:3002"}, server_ids);

        // Step 4: Log system configuration
        // - Display actor IDs for debugging
        // - Show listening ports
        // - Confirm initialization
        qb::io::cout() << "Chat server started on ports 3001 and 3002" << std::endl;
        qb::io::cout() << "ChatRoom ID: " << chatroom_id << std::endl;
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