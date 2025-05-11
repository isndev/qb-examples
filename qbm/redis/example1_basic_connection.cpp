/**
 * @file examples/qbm/redis/example1_basic_connection.cpp
 * @example qbm-redis: Basic Connection and String Operations
 *
 * @brief This example demonstrates fundamental usage of the `qbm-redis` client library,
 * including connecting to a Redis server and performing common string operations.
 * It runs as a standalone application, relying on `qb-io` for asynchronous support.
 *
 * @details
 * The program performs the following steps:
 * 1.  Initializes the QB asynchronous I/O system using `qb::io::async::init()`.
 * 2.  Creates an instance of `qb::redis::tcp::client`, configured with `REDIS_URI`.
 * 3.  Attempts to connect to the Redis server using `redis.connect()`.
 * 4.  If connected successfully, it demonstrates several Redis string commands:
 *     -   `redis.set("example:greeting", "Hello, Redis!")`: Sets a key-value pair.
 *     -   `redis.get("example:greeting")`: Retrieves the value for a key.
 *     -   `redis.set("example:counter", "10")` followed by `redis.incr("example:counter")`:
 *         Atomically increments a numeric value stored as a string.
 *     -   `redis.setex("example:temporary", 60, "This will expire...")`: Sets a key with a
 *         60-second expiration time.
 *     -   `redis.del(...)`: Deletes the keys created during the example.
 * 5.  Output messages indicate the success or failure of operations and display retrieved values.
 *
 * Note: This example uses synchronous calls to the Redis client (e.g., `redis.set()` blocks
 * until the command is acknowledged). For use within QB actors, asynchronous methods
 * (`redis.set_async(...)` or the variants taking a callback) should be preferred to avoid
 * blocking the actor's event loop.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::io::async::init()`: Initialization for standalone `qb-io` applications.
 * - `qb::redis::tcp::client`: The primary Redis client class.
 * - `client.connect()`: Establishing a connection to the Redis server.
 * - Basic Redis String Commands:
 *   - `client.set(key, value)`
 *   - `client.get(key)` (returns `std::optional<std::string>`)
 *   - `client.incr(key)` (returns `long long`)
 *   - `client.setex(key, seconds, value)`
 *   - `client.del(key, ...)` (returns `long long` - number of keys deleted)
 * - `qb::io::cout()`: Thread-safe console output.
 * - URI-based client configuration (implicitly via constructor).
 */

#include <redis/redis.h>
#include <qb/io/async.h>
#include <iostream>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

int main() {
    // Initialize the async system (required for standalone apps)
    qb::io::async::init();
    
    // Create Redis client
    qb::redis::tcp::client redis{REDIS_URI};
    
    // Connect to Redis - returns boolean success
    if (!redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis" << std::endl;
        return 1;
    }
    
    qb::io::cout() << "Connected to Redis successfully!" << std::endl;
    
    // Basic SET operation
    if (!redis.set("example:greeting", "Hello, Redis!")) {
        qb::io::cerr() << "SET operation failed" << std::endl;
        return 1;
    }
    qb::io::cout() << "SET operation successful" << std::endl;
    
    // Basic GET operation
    auto get_result = redis.get("example:greeting");
    if (get_result.has_value()) {
        qb::io::cout() << "Retrieved value: " << *get_result << std::endl;
    } else {
        qb::io::cerr() << "Key not found or GET operation failed" << std::endl;
        return 1;
    }
    
    // Using INCR for atomic counter operations
    redis.set("example:counter", "10");
    auto incr_result = redis.incr("example:counter");
    qb::io::cout() << "Counter value after INCR: " << incr_result << std::endl;
    
    // Setting expiration on a key
    if (!redis.setex("example:temporary", 60, "This will expire in 60 seconds")) {
        qb::io::cerr() << "SETEX operation failed" << std::endl;
        return 1;
    }
    qb::io::cout() << "Set key with 60-second expiration" << std::endl;
    
    // Delete keys when done
    auto deleted = redis.del("example:greeting", "example:counter", "example:temporary");
    qb::io::cout() << "Deleted " << deleted << " keys" << std::endl;
    
    qb::io::cout() << "Basic Redis operations completed successfully!" << std::endl;
    
    return 0;
} 