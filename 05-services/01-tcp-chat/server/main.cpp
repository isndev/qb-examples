/**
 * @file examples/05-services/01-tcp-chat/server/main.cpp
 * @tier 05-services
 * @teaches How a real server is laid out across cores: the acceptor on one, a pool of session
 *          actors on another, the room's state on a third — and one builder() chain that names
 *          the pool so the acceptor can round-robin over it.
 * @demonstrates qb::Main, addActor<T>, engine.core, builder(), idList(), qb::io::uri,
 *               engine.start, engine.hasError, qb::Main::stop, engine.join, qb::io::cout, qb::io::cerr
 * @prerequisites 01-actors/04-cores-and-placement, 02-io/03-tcp
 * @expect "Chat server started on ports 3001 and 3002"
 * @expect "Engine is running"
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
 * 6.  Watches a cancellable console reader while a separate thread joins the engine.
 *     Enter requests `qb::Main::stop()`; EOF disables console control without stopping
 *     the service. External engine shutdown also releases main with partial stdin.
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
 * - Graceful Shutdown: `qb::Main::stop()` followed by `engine.join()`.
 * - `qb::io::uri`: For specifying listen addresses for `AcceptActor`s.
 */

#include <qb/main.h>
#include "AcceptActor.h"
#include "ServerActor.h"
#include "ChatRoomActor.h"
#include "../../../common/CancelableLineSource.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int
main(int argc, char *argv[]) {
    if (auto helper = qb_example::input::maybe_run_helper(argc, argv)) {
        return *helper;
    }

    try {
        // Input remains alive until the engine and its observer have stopped.
        qb_example::input::LineSource input;
        qb::Main                      engine;
        // Step 1: Create ChatRoomActor (Core 3)
        // - Central component for state management
        // - Placed on separate core to handle broadcasts
        auto chatroom_id = engine.addActor<ChatRoomActor>(3);

        // Step 2: Create ServerActors (Core 1)
        // - Multiple instances for connection handling
        // - Share chatroom_id for message routing
        // - Builder pattern for multiple actors
        auto server_ids = engine.core(1).builder().addActor<ServerActor>(chatroom_id).addActor<ServerActor>(chatroom_id).idList();

        // Step 3: Create AcceptActors (Core 0)
        // - Multiple listeners on different ports
        // - Share server_ids for connection distribution
        // - Demonstrate QB's URI-based configuration
        engine.core(0)
            .builder()
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
        engine.start(true); // asynchronous start
        // start(true) WAITS for the init barrier: every core has either reached its event
        // loop or recorded why it could not, so hasError() is answerable RIGHT HERE. A
        // server whose acceptor failed to bind must say so through its exit status --
        // printing "Engine is running" over cores that never started is how a failed bind
        // reports success to whatever supervises this process.
        if (engine.hasError()) {
            qb::io::cerr() << "Engine failed to start: an actor failed its init (see log above)" << std::endl;
            engine.join();
            input.stop();
            return 1;
        }
        qb::io::cout() << "Engine is running" << std::endl;
        qb::io::cout() << "Press Enter to stop the engine" << std::endl;
        std::atomic<bool> engine_finished{false};
        std::thread       observer([&] {
            engine.join();
            engine_finished.store(true, std::memory_order_release);
        });

        try {
            while (!engine_finished.load(std::memory_order_acquire)) {
                qb_example::input::Event event;
                if (!input.wait_pop(event, std::chrono::milliseconds{50})) {
                    continue;
                }
                if (event.kind == qb_example::input::Event::Kind::line) {
                    qb::Main::stop();
                } else if (event.kind == qb_example::input::Event::Kind::error) {
                    qb::io::cerr() << "Console input failed; stopping server" << std::endl;
                    qb::Main::stop();
                }
                // EOF merely disables console control; SIGTERM/SIGINT still stop the engine.
            }
        } catch (...) {
            qb::Main::stop();
            observer.join();
            input.stop();
            throw;
        }
        observer.join();
        input.stop();
        return engine.hasError() ? 1 : 0;
    } catch (const std::exception &e) {
        qb::io::cerr() << "Error: " << e.what() << std::endl;
        return 1;
    }
}
