/**
 * @file AcceptActor.h
 * @brief Connection acceptor demonstrating QB's TCP server capabilities
 * 
 * This file demonstrates:
 * 1. How to implement a TCP acceptor using QB's I/O framework
 * 2. How to distribute connections across multiple server actors
 * 3. How to handle connection events in an actor system
 * 4. How to implement load balancing in QB
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