/**
 * @file examples/qbm/redis/coro_example.cpp
 * @brief Example demonstrating Redis coroutine API with pure qb-io
 * 
 * This example shows how to use the coroutine-based Redis API
 * for clean, linear async code without callback hell.
 * 
 * PURE QB-IO - NO ACTORS!
 */

#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>
#include <qbm/redis/coro/client.h>
#include <iostream>
#include <memory>

/**
 * @brief Example 1: Simple GET/SET operations
 */
qb::io::async::task<void> example_simple_get_set() {
    std::cout << "\n=== Example 1: Simple GET/SET ===" << std::endl;
    
    // Create Redis client
    auto redis = std::make_shared<qb::redis::tcp::client>();
    if (!redis->connect("tcp://localhost:6379")) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }
    
    // Create coroutine wrapper
    auto coro = qb::redis::coro::client(*redis);
    
    // Set a value - linear code!
    auto set_reply = co_await coro.set("user:1:name", "Alice");
    if (set_reply.ok()) {
        std::cout << "✓ SET user:1:name = Alice" << std::endl;
    } else {
        std::cerr << "✗ SET failed" << std::endl;
    }
    
    // Get the value back
    auto get_reply = co_await coro.get("user:1:name");
    if (get_reply.ok() && get_reply.result()->has_value()) {
        std::cout << "✓ GET user:1:name = " << get_reply.result()->value() << std::endl;
    } else {
        std::cerr << "✗ GET failed or key not found" << std::endl;
    }
}

/**
 * @brief Example 2: Sequential operations with error handling
 */
qb::io::async::task<void> example_sequential_operations() {
    std::cout << "\n=== Example 2: Sequential Operations ===" << std::endl;
    
    auto redis = std::make_shared<qb::redis::tcp::client>();
    if (!redis->connect("tcp://localhost:6379")) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }
    
    auto coro = qb::redis::coro::client(*redis);
    
    try {
        // Setup test data
        co_await coro.hset("user:123", "name", "Bob");
        co_await coro.hset("user:123", "email", "bob@example.com");
        co_await coro.hset("user:123", "age", "30");
        
        std::cout << "✓ User data created" << std::endl;
        
        // Fetch user profile - multiple operations in sequence
        auto name = co_await coro.hget("user:123", "name");
        auto email = co_await coro.hget("user:123", "email");
        auto age = co_await coro.hget("user:123", "age");
        
        if (name.ok() && email.ok() && age.ok()) {
            std::cout << "✓ User Profile:" << std::endl;
            std::cout << "  Name:  " << name.result()->value_or("N/A") << std::endl;
            std::cout << "  Email: " << email.result()->value_or("N/A") << std::endl;
            std::cout << "  Age:   " << age.result()->value_or("N/A") << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Error: " << e.what() << std::endl;
    }
}

/**
 * @brief Example 3: Multiple keys operations
 */
qb::io::async::task<void> example_multiple_keys() {
    std::cout << "\n=== Example 3: Multiple Keys ===" << std::endl;
    
    auto redis = std::make_shared<qb::redis::tcp::client>();
    if (!redis->connect("tcp://localhost:6379")) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }
    
    auto coro = qb::redis::coro::client(*redis);
    
    // Set multiple keys
    co_await coro.set("key1", "value1");
    co_await coro.set("key2", "value2");
    co_await coro.set("key3", "value3");
    
    std::cout << "✓ Created 3 keys" << std::endl;
    
    // Fetch them sequentially (could be parallel with when_all)
    auto r1 = co_await coro.get("key1");
    auto r2 = co_await coro.get("key2");
    auto r3 = co_await coro.get("key3");
    
    int success = 0;
    if (r1.ok() && r1.result()->has_value()) ++success;
    if (r2.ok() && r2.result()->has_value()) ++success;
    if (r3.ok() && r3.result()->has_value()) ++success;
    
    std::cout << "✓ Fetched " << success << "/3 keys successfully" << std::endl;
    
    // Cleanup
    co_await coro.del({"key1", "key2", "key3"});
    std::cout << "✓ Cleaned up test keys" << std::endl;
}

/**
 * @brief Example 4: Error handling patterns
 */
qb::io::async::task<void> example_error_handling() {
    std::cout << "\n=== Example 4: Error Handling ===" << std::endl;
    
    auto redis = std::make_shared<qb::redis::tcp::client>();
    if (!redis->connect("tcp://localhost:6379")) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        co_return;
    }
    
    auto coro = qb::redis::coro::client(*redis);
    
    // Try to get non-existent key
    auto reply = co_await coro.get("non_existent_key");
    
    if (reply.ok()) {
        if (reply.result()->has_value()) {
            std::cout << "Value: " << reply.result()->value() << std::endl;
        } else {
            std::cout << "✓ Key not found (expected)" << std::endl;
        }
    } else {
        std::cerr << "✗ Redis error: " << reply.error() << std::endl;
    }
}

/**
 * @brief Main coroutine that runs all examples
 */
qb::io::async::task<void> run_all_examples() {
    std::cout << "QB Redis Coroutine Examples (Pure qb-io)" << std::endl;
    std::cout << "Make sure Redis is running on localhost:6379" << std::endl;
    
    co_await example_simple_get_set();
    co_await example_sequential_operations();
    co_await example_multiple_keys();
    co_await example_error_handling();
    
    std::cout << "\n=== All Examples Complete ===" << std::endl;
}

int main(int argc, char* argv[]) {
    // Initialize qb-io async system
    qb::io::async::init();
    
    // Spawn the main coroutine
    qb::io::async::spawn(run_all_examples());
    
    // Run event loop
    qb::io::async::run();
    
    std::cout << "\nExamples completed successfully!" << std::endl;
    return 0;
}
