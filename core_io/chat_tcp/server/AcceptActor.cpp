/**
 * @file AcceptActor.cpp
 * @brief Implementation demonstrating QB's TCP acceptor patterns
 * 
 * This file demonstrates:
 * 1. How to implement a TCP acceptor in QB
 * 2. How to handle connection distribution
 * 3. How to manage server pool
 * 4. How to handle acceptor lifecycle
 */

#include "AcceptActor.h"
#include "../shared/Events.h"
#include <iostream>

/**
 * Constructor showing QB's acceptor configuration:
 * 1. URI Configuration:
 *    - Uses QB's URI system for network setup
 *    - Supports flexible binding options
 *    - Enables runtime configuration
 * 
 * 2. Server Pool:
 *    - Manages multiple server instances
 *    - Enables load distribution
 *    - Supports scaling
 */
AcceptActor::AcceptActor(qb::io::uri listen_at, qb::ActorIdList pool)
    : _listen_at(std::move(listen_at))
    , _server_pool(std::move(pool)) {}

/**
 * Initialization demonstrating QB's acceptor setup:
 * 1. Validation:
 *    - Checks server pool
 *    - Validates configuration
 *    - Ensures proper setup
 * 
 * 2. Listener Setup:
 *    - Binds to specified address
 *    - Configures TCP listener
 *    - Starts accepting connections
 * 
 * 3. Error Handling:
 *    - Validates server pool
 *    - Handles bind failures
 *    - Reports initialization status
 */
bool AcceptActor::onInit() {
    // Validate server pool
    if (_server_pool.empty()) {
        std::cerr << "Cannot init AcceptActor with empty server pool" << std::endl;
        return false;
    }

    // Set up TCP listener
    if (transport().listen(_listen_at)) {
        std::cerr << "Cannot listen on " << _listen_at.source() << std::endl;
        return false;
    }

    std::cout << "AcceptActor listening on " << _listen_at.source() << std::endl;
    start();  // Start accepting connections
    return true;
}

/**
 * Connection handler demonstrating QB's connection distribution:
 * 1. Load Balancing:
 *    - Round-robin distribution
 *    - Tracks connection count
 *    - Balances server load
 * 
 * 2. Socket Handling:
 *    - Moves connected socket
 *    - Creates session event
 *    - Routes to target server
 * 
 * 3. Event Creation:
 *    - Generates NewSessionEvent
 *    - Transfers socket ownership
 *    - Maintains proper lifecycle
 */
void AcceptActor::on(accepted_socket_type&& new_io) {
    // Distribute new connection to next server in pool (round-robin)
    auto server_id = _server_pool[_session_counter++ % _server_pool.size()];
    
    // Forward socket to selected server
    auto& evt = push<NewSessionEvent>(server_id);
    evt.socket = std::move(new_io);
}

/**
 * Disconnection handler showing QB's shutdown patterns:
 * 1. Shutdown Handling:
 *    - Detects acceptor failure
 *    - Initiates system shutdown
 *    - Ensures clean termination
 * 
 * 2. Notification:
 *    - Logs disconnection
 *    - Broadcasts shutdown
 *    - Coordinates cleanup
 * 
 * 3. System Management:
 *    - Handles unexpected failures
 *    - Maintains system state
 *    - Enables recovery
 */
void AcceptActor::on(qb::io::async::event::disconnected const&) {
    // Handle listener socket disconnection by shutting down the system
    std::cout << "AcceptActor disconnected" << std::endl;
    broadcast<qb::KillEvent>();  // Broadcast shutdown signal to all actors
} 