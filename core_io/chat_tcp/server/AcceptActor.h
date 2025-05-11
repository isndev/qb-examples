/**
 * @file examples/core_io/chat_tcp/server/AcceptActor.h
 * @example TCP Chat Server - Connection Acceptor Actor
 * @brief Actor responsible for accepting incoming TCP connections and distributing
 * them to a pool of `ServerActor`s for session management.
 *
 * @details
 * This actor utilizes QB-IO's TCP acceptor capabilities to listen on a specified
 * network address and port. When a new client connection is established, the
 * `AcceptActor` employs a round-robin strategy to select a `ServerActor` from
 * a predefined pool and forwards the new connection (socket) to it via a
 * `NewSessionEvent`.
 *
 * It inherits from `qb::Actor` for QB framework integration and
 * `qb::io::use<AcceptActor>::tcp::acceptor` to gain TCP listening and connection
 * acceptance functionalities.
 *
 * QB Features Demonstrated:
 * - `qb::Actor`: Base class for concurrent entities.
 * - `qb::io::use<AcceptActor>::tcp::acceptor`: Mixin providing TCP acceptor capabilities.
 *   - `transport().listen()`: To bind to an address and start listening.
 *   - `start()`: To begin accepting connections.
 *   - `on(accepted_socket_type&& new_io)`: Callback for newly accepted connections.
 * - `qb::io::uri`: For specifying the listening address (e.g., "tcp://0.0.0.0:8080").
 * - `qb::ActorIdList`: To manage a pool of `ServerActor` IDs for load distribution.
 * - Inter-Actor Communication: `push<NewSessionEvent>(server_id)` to delegate connection handling.
 * - System Event Handling: `on(qb::io::async::event::disconnected const&)` to handle listener socket issues,
 *   typically by initiating a system shutdown (`broadcast<qb::KillEvent>()`).
 */

#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/uri.h>

/**
 * @brief Actor that listens for and accepts new TCP connections
 * 
 * AcceptActor demonstrates QB's server-side networking:
 * 1. TCP Acceptor Integration:
 *    - Inherits from qb::io::use<AcceptActor>::tcp::acceptor
 *    - Handles incoming TCP connections
 *    - Manages listening socket lifecycle
 * 
 * 2. Load Balancing:
 *    - Distributes connections across multiple ServerActors
 *    - Implements round-robin distribution
 *    - Maintains connection count balance
 * 
 * 3. QB Actor Integration:
 *    - Uses QB's event system for connection notification
 *    - Manages server pool through ActorId references
 *    - Handles system shutdown properly
 * 
 * 4. Error Handling:
 *    - Validates server pool
 *    - Handles listen failures
 *    - Manages disconnection scenarios
 */
class AcceptActor : public qb::Actor,
                    public qb::io::use<AcceptActor>::tcp::acceptor {
private:
    const qb::io::uri _listen_at;      // URI specifying the listening address and port
    const qb::ActorIdList _server_pool; // List of ServerActors for connection distribution
    std::size_t _session_counter{0};    // Counter for round-robin load balancing

public:
    /**
     * @brief Constructs a new accept actor
     * 
     * Demonstrates QB's URI-based configuration:
     * 1. Uses qb::io::uri for network configuration
     * 2. Supports multiple server instances
     * 3. Enables flexible deployment
     * 
     * @param listen_at URI specifying where to listen (e.g., "tcp://0.0.0.0:8080")
     * @param pool List of ServerActor IDs for connection distribution
     */
    AcceptActor(qb::io::uri listen_at, qb::ActorIdList pool);
    
    /**
     * @brief Initializes the accept actor
     * 
     * QB initialization sequence:
     * 1. Validates server pool configuration
     * 2. Sets up TCP listener using QB's I/O framework
     * 3. Starts accepting connections
     * 
     * @return true if successfully listening, false on configuration or network errors
     */
    bool onInit() override;

    /**
     * @brief Handles new TCP connections
     * 
     * QB's acceptor framework calls this when:
     * 1. New client connection arrives
     * 2. TCP handshake completes
     * 3. Socket is ready for I/O
     * 
     * Demonstrates QB's connection handling:
     * 1. Creates NewSessionEvent
     * 2. Selects target ServerActor
     * 3. Transfers socket ownership
     * 
     * @param new_io The newly accepted socket, moved to target ServerActor
     */
    void on(accepted_socket_type&& new_io);

    /**
     * @brief Handles acceptor disconnection
     * 
     * QB calls this when:
     * 1. Listening socket closes
     * 2. Network interface fails
     * 3. System requests shutdown
     * 
     * Demonstrates proper QB shutdown:
     * 1. Logs disconnection
     * 2. Broadcasts shutdown signal
     * 3. Ensures clean system termination
     * 
     * @param The QB disconnection event
     */
    void on(qb::io::async::event::disconnected const&);
}; 