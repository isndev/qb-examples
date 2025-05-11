/**
 * @file examples/qbm/redis/example2_hash_operations.cpp
 * @example qbm-redis: Hash Data Structure Operations
 *
 * @brief This example demonstrates how to use Redis Hash commands via the `qbm-redis`
 * client library to store, retrieve, and manage structured data, such as a user profile.
 * It runs as a standalone `qb-io` based application.
 *
 * @details
 * The program performs the following sequence of operations:
 * 1.  Initializes the QB asynchronous I/O system using `qb::io::async::init()`.
 * 2.  Creates a `qb::redis::tcp::client` and connects to the Redis server.
 * 3.  Defines a unique key for a user profile hash (e.g., "example:user:profile:1001").
 * 4.  Optionally cleans up any pre-existing data at this key using `redis.del()`.
 * 5.  **Setting Hash Fields**:
 *     -   Uses multiple `redis.hset(key, field, value)` calls to populate the user profile
 *         with fields like "username", "first_name", "email", "age", etc.
 * 6.  **Getting Hash Fields**:
 *     -   `redis.hget(key, field)`: Retrieves a single field's value.
 *     -   The example simulates `HMGET` by calling `hget` multiple times. `qbm-redis` directly
 *       supports `redis.hmget(key, field1, field2, ...)` which returns `std::vector<std::optional<std::string>>`.
 * 7.  **Checking Field Existence**:
 *     -   `redis.hexists(key, field)`: Checks if a field exists within the hash.
 * 8.  **Incrementing Numeric Fields**:
 *     -   `redis.hincrby(key, field, increment)`: Atomically increments a numeric value
 *         stored in a hash field (e.g., "age").
 * 9.  **Getting All Data**:
 *     -   `redis.hgetall(key)`: Retrieves all field-value pairs from the hash as a
 *         `std::map<std::string, std::string>`.
 * 10. **Getting Keys/Values/Length**:
 *     -   `redis.hkeys(key)`: Returns a `std::vector<std::string>` of all field names.
 *     -   `redis.hvals(key)`: Returns a `std::vector<std::string>` of all field values.
 *     -   `redis.hlen(key)`: Returns the number of fields in the hash.
 * 11. Cleans up the test key using `redis.del()`.
 *
 * This example uses synchronous Redis client calls. For actor-based applications,
 * asynchronous equivalents with callbacks should be used.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::redis::tcp::client` and `client.connect()`.
 * - Redis Hash commands:
 *   - `client.hset(key, field, value)`
 *   - `client.hget(key, field)`
 *   - `client.hexists(key, field)`
 *   - `client.hincrby(key, field, increment)`
 *   - `client.hgetall(key)`
 *   - `client.hkeys(key)`
 *   - `client.hvals(key)`
 *   - `client.hlen(key)`
 *   - `client.del(key)`
 * - `qb::io::async::init()` and `qb::io::cout()`.
 */

#include <redis/redis.h>
#include <qb/io/async.h>
#include <iostream>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

int main() {
    // Initialize the async system for standalone apps
    qb::io::async::init();
    auto cout = qb::io::cout();
    
    // Create Redis client
    qb::redis::tcp::client redis{REDIS_URI};
    
    // Connect to Redis - returns boolean success
    if (!redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis" << std::endl;
        return 1;
    }
    
    cout << "Connected to Redis successfully!" << std::endl;
    
    // Generate a unique key for this example to avoid collisions
    std::string user_key = "example:user:profile:1001";
    
    // Clean up any existing data
    redis.del(user_key);
    
    // -------- Setting multiple hash fields at once --------
    // Create a user profile using individual HSET calls
    int hset_count = 0;
    hset_count += redis.hset(user_key, "username", "johndoe") ? 1 : 0;
    hset_count += redis.hset(user_key, "first_name", "John") ? 1 : 0;
    hset_count += redis.hset(user_key, "last_name", "Doe") ? 1 : 0;
    hset_count += redis.hset(user_key, "email", "john.doe@example.com") ? 1 : 0;
    hset_count += redis.hset(user_key, "active", "1") ? 1 : 0;
    hset_count += redis.hset(user_key, "age", "30") ? 1 : 0;
    hset_count += redis.hset(user_key, "registration_date", "2023-01-15") ? 1 : 0;
    
    cout << "Added " << hset_count << " new fields to hash" << std::endl;
    
    // -------- Getting a single field from the hash --------
    auto username = redis.hget(user_key, "username");
    if (username.has_value()) {
        cout << "Username: " << *username << std::endl;
    } else {
        cout << "Username field not found" << std::endl;
    }
    
    // -------- Getting multiple fields at once --------
    auto first_name = redis.hget(user_key, "first_name");
    auto last_name = redis.hget(user_key, "last_name");
    auto nonexistent = redis.hget(user_key, "nonexistent_field");
    
    cout << "HMGET results:" << std::endl;
    if (first_name.has_value()) {
        cout << "  first_name: " << *first_name << std::endl;
    } else {
        cout << "  first_name: (nil)" << std::endl;
    }
    
    if (last_name.has_value()) {
        cout << "  last_name: " << *last_name << std::endl;
    } else {
        cout << "  last_name: (nil)" << std::endl;
    }
    
    if (nonexistent.has_value()) {
        cout << "  nonexistent_field: " << *nonexistent << std::endl;
    } else {
        cout << "  nonexistent_field: (nil)" << std::endl;
    }
    
    // -------- Checking if a field exists --------
    bool email_exists = redis.hexists(user_key, "email");
    cout << "Email field exists: " << (email_exists ? "true" : "false") << std::endl;
    
    // -------- Incrementing numeric fields --------
    auto new_age = redis.hincrby(user_key, "age", 1);
    cout << "Age after increment: " << new_age << std::endl;
    
    // -------- Getting all fields and values --------
    auto all_data = redis.hgetall(user_key);
    cout << "All hash fields for " << user_key << ":" << std::endl;
    for (const auto& [field, value] : all_data) {
        cout << "  " << field << ": " << value << std::endl;
    }
    
    // -------- Getting only field names or values --------
    auto fields_list = redis.hkeys(user_key);
    cout << "Hash fields: ";
    for (size_t i = 0; i < fields_list.size(); i++) {
        cout << fields_list[i] << (i < fields_list.size() - 1 ? ", " : "");
    }
    cout << std::endl;
    
    auto values_list = redis.hvals(user_key);
    cout << "Hash values: ";
    for (size_t i = 0; i < values_list.size(); i++) {
        cout << values_list[i] << (i < values_list.size() - 1 ? ", " : "");
    }
    cout << std::endl;
    
    // -------- Getting the number of fields --------
    auto fields_count = redis.hlen(user_key);
    cout << "Hash now has " << fields_count << " fields" << std::endl;
    
    // Clean up
    redis.del(user_key);
    cout << "Hash operations completed successfully!" << std::endl;
    
    return 0;
} 