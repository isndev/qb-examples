/**
 * @file examples/core_io/chat_tcp/server/main.cpp
 * @example TCP Chat Server - Application Entry Point
 * @brief Main entry point for the TCP chat server application.
 *
 * @details
 * This file sets up and launches the server-side actor system for the TCP chat application.
 * It demonstrates a multi-core deployment strategy for different server components:
 * 1.  Initializes the `qb::Main` engine.
 * 2.  Creates the `ChatRoomActor` on a dedicated core (core 3). This actor manages the
 *     central chat logic and state.
 * 3.  Creates a pool of `ServerActor`s on another dedicated core (core 1). These actors
 *     are responsible for handling individual client connections and I/O, delegating
 *     application logic to the `ChatRoomActor`.
 * 4.  Creates multiple `AcceptActor`s on a third core (core 0). Each `AcceptActor` listens
 *     on a different port (e.g., 3001 and 3002) and distributes incoming connections
 *     to the pool of `ServerActor`s in a round-robin fashion.
 * 5.  Starts the QB engine asynchronously (`engine.start(true)`).
 * 6.  Waits for user input (Enter key) to gracefully shut down the server by calling
 *     `engine.stop()` and then `engine.join()`.
 *
 * This architecture aims for scalability and separation of concerns:
 * - Connection acceptance is handled by `AcceptActor`s.
 * - I/O and session management for individual clients are handled by `ServerActor`s.
 * - Core chat application logic and state are centralized in `ChatRoomActor`.
 * Distributing these across different cores can improve performance under load.
 *
 * QB Features Demonstrated:
 * - `qb::Main`: The main engine for the actor system.
 * - `engine.addActor<ActorType>(core_id, args...)`: Creating actors and assigning them to specific CPU cores.
 * - `engine.core(core_id).builder()`: Fluent API for adding multiple actors to the same core.
 * - `qb::ActorIdList`: Used to pass the list of `ServerActor` IDs to `AcceptActor`s.
 * - Multi-Core Actor Deployment: Strategically placing actors on different cores.
 * - Asynchronous Engine Start: `engine.start(true)`.
 * - Graceful Shutdown: `engine.stop()` followed by `engine.join()`.
 * - `qb::io::uri`: For specifying listen addresses for `AcceptActor`s.
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