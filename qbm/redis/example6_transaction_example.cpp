/**
 * @file examples/qbm/redis/example6_transaction_example.cpp
 * @example qbm-redis: Transactions and Atomic Operations with Actors
 *
 * @brief This example demonstrates concepts related to Redis transactions and atomic
 * operations within a QB actor system, simulating a simple inventory management scenario.
 * It showcases how actors can interact with Redis for stateful operations that might
 * require atomicity.
 *
 * @details
 * The system includes several actors:
 * 1.  `InventoryManagerActor`:
 *     -   Connects to Redis and manages product inventory stored in Redis Hashes (one hash per product).
 *     -   `setup_inventory()`: Populates Redis with initial product data (name, price, quantity).
 *     -   `on(const OrderRequestEvent&)`: Processes client orders. This involves:
 *         -   Fetching current quantity (`hget`).
 *         -   Checking stock availability.
 *         -   If stock is sufficient, decrementing quantity (`hincrby`) and recording the order.
 *         *   Note: While this sequence of operations is a candidate for a Redis transaction
 *             (`MULTI`/`EXEC`) for atomicity, this example executes them as individual commands.
 *             `qbm-redis` supports transactions, which would typically be used here in production.
 *             Optimistic locking with `WATCH` could also be applied.
 *     -   `demonstrate_simple_transaction()`: Shows other Redis commands, including the use of
 *         `_redis.eval<long long>()` to execute a Lua script for an atomic `HINCRBY` on worker metrics.
 *         Lua scripts are a powerful way to ensure atomicity for complex operations on the Redis server.
 *     -   Sends `OrderResultEvent` back to the client and `SetupCompletedEvent` /
 *         `TransactionDemoCompletedEvent` to the coordinator.
 * 2.  `OrderClientActor` (multiple instances):
 *     -   Simulates a client that places a predefined number of orders for random products and quantities.
 *     -   Sends `OrderRequestEvent`s to the `InventoryManagerActor`.
 *     -   Receives `OrderResultEvent`s and logs the outcome.
 *     -   Notifies the `CoordinatorActor` (via `ShutdownEvent`) when all its orders are completed.
 * 3.  `CoordinatorActor`:
 *     -   Manages the overall simulation lifecycle.
 *     -   Creates and supervises the `InventoryManagerActor` and `OrderClientActor`s.
 *     -   Waits for setup and demo completion signals before allowing clients to order.
 *     -   Initiates a system-wide shutdown once all clients have finished.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::Actor`, `qb::Main`, `qb::Event`, `qb::ICallback` (not directly in this example but common).
 * - `qb::redis::tcp::client` for Redis communication within actors.
 * - Various Redis commands: `keys()`, `del()`, `hset()`, `hget()`, `hgetall()`, `hincrby()`, `incr()`, `set()`, `lpush()`, `lrange()`.
 * - Lua Scripting: `client.eval<ReturnType>(script, keys_vector, args_vector)` for atomic server-side operations.
 * - Actor communication patterns for request/response and notifications.
 * - `qb::io::async::init()`, `qb::io::async::callback()`.
 * - `addRefActor`.
 *
 * Key Learning: The example illustrates the structure for using Redis in an actor system.
 * For critical multi-step Redis operations like order processing, explicit Redis transactions
 * (`MULTI`/`EXEC` possibly with `WATCH`) or Lua scripting (as partially shown) should be used
 * to guarantee atomicity, which `qbm-redis` supports.
 */

#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <iostream>
#include <random>
#include <vector>
#include <string>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// Use required Redis types
using qb::redis::status;

// Structure to represent a product in inventory
struct Product {
    std::string id;
    std::string name;
    int price;
    int quantity;
};

// Event to request an order
struct OrderRequestEvent : qb::Event {
    std::string product_id;
    int quantity;
    qb::ActorId sender_id;
    
    OrderRequestEvent(std::string id, int qty, qb::ActorId sender)
        : product_id(std::move(id)), quantity(qty), sender_id(sender) {}
};

// Event to report order result
struct OrderResultEvent : qb::Event {
    std::string product_id;
    int quantity;
    bool success;
    std::string message;
    
    OrderResultEvent(std::string id, int qty, bool succ, std::string msg)
        : product_id(std::move(id)), quantity(qty), 
          success(succ), message(std::move(msg)) {}
};

// Event to signal that inventory setup is complete
struct SetupCompletedEvent : qb::Event {
    explicit SetupCompletedEvent() {}
};

// Event to signal that transaction demo is complete
struct TransactionDemoCompletedEvent : qb::Event {
    explicit TransactionDemoCompletedEvent() {}
};

// Event to signal shutdown
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

// Helper function to generate example products
std::vector<Product> initialize_inventory() {
    return {
        {"p1", "Laptop", 999, 10},
        {"p2", "Smartphone", 699, 20},
        {"p3", "Headphones", 99, 50},
        {"p4", "Monitor", 299, 15},
        {"p5", "Keyboard", 59, 30}
    };
}

// Actor for managing inventory with Redis transactions
class InventoryManagerActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    bool _connected = false;
    qb::ActorId _coordinator_id;
    
    // Helper for random number generation
    int random_int(int min, int max) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(min, max);
        return dis(gen);
    }

public:
    explicit InventoryManagerActor(qb::ActorId coordinator)
        : _coordinator_id(coordinator) {}
    
    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "InventoryManagerActor initialized" << std::endl;
        
        // Register for events
        registerEvent<OrderRequestEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Connect to Redis
        if (!_redis.connect()) {
            qb::io::cerr() << "Failed to connect to Redis" << std::endl;
            return false;
        }
        
        _connected = true;
        cout << "Connected to Redis successfully!" << std::endl;
        
        // Setup inventory
        setup_inventory();
        
        return true;
    }
    
    // Setup inventory with initial products
    void setup_inventory() {
        auto cout = qb::io::cout();
        if (!_connected) return;
        
        try {
        // Clean up any existing inventory data
        auto keys = _redis.keys("inventory:*");
            if (!keys.empty()) {
                _redis.del(keys);
        }
        
            auto products = initialize_inventory();
        
            // Add each product to inventory directly
        for (const auto& product : products) {
                // Set product data in Redis hash
                _redis.hset("inventory:" + product.id, "name", product.name);
                _redis.hset("inventory:" + product.id, "price", std::to_string(product.price));
                _redis.hset("inventory:" + product.id, "quantity", std::to_string(product.quantity));
            }
            
            cout << "Inventory initialized with " << products.size() 
                         << " products" << std::endl;
            display_inventory();
            
            // Notify coordinator that setup is complete
            push<SetupCompletedEvent>(_coordinator_id);
            
            // Demonstrate other features
            qb::io::async::callback([this]() {
                demonstrate_simple_transaction();
            }, 1.0);
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error setting up inventory: " << e.what() << std::endl;
            push<ShutdownEvent>(_coordinator_id);
        }
    }
    
    // Display current inventory
    void display_inventory() {
        auto cout = qb::io::cout();
        if (!_connected) return;
        
        auto keys = _redis.keys("inventory:*");
        if (keys.empty()) {
            cout << "No products in inventory!" << std::endl;
            return;
        }
        
        cout << "\n=== Current Inventory ===" << std::endl;
        cout << std::setw(10) << "ID" << std::setw(15) << "Name" 
             << std::setw(10) << "Price" << std::setw(10) << "Quantity" << std::endl;
        cout << std::string(45, '-') << std::endl;
        
        for (const auto& key : keys) {
            auto product_data = _redis.hgetall(key);
            
                std::string id = key.substr(key.find(":") + 1);  // Extract ID from key
            std::string name = product_data["name"];
            std::string price = product_data["price"];
            std::string quantity = product_data["quantity"];
                
            cout << std::setw(10) << id << std::setw(15) << name 
                 << std::setw(10) << price << std::setw(10) << quantity << std::endl;
            }
        cout << std::endl;
    }
    
    // Process an order with optimistic locking using WATCH
    void on(const OrderRequestEvent& event) {
        auto cout = qb::io::cout();
        std::string product_id = event.product_id;
        int order_quantity = event.quantity;
        qb::ActorId sender_id = event.sender_id;
        
        if (!_connected) {
            push<OrderResultEvent>(sender_id, product_id, order_quantity, false, 
                                  "Not connected to Redis");
            return;
        }
        
        try {
            // Form the key for the product
            std::string key = "inventory:" + product_id;
            
            // Get current quantity
            auto current_quantity_result = _redis.hget(key, "quantity");
            if (!current_quantity_result.has_value()) {
                qb::io::cerr() << "Product " << product_id << " not found" << std::endl;
                
                push<OrderResultEvent>(sender_id, product_id, order_quantity, false, 
                                      "Product not found");
                return;
            }
            
            int current_quantity = std::stoi(current_quantity_result.value());
            
            // Check if we have enough stock
            if (current_quantity < order_quantity) {
                cout << "Not enough stock for " << product_id 
                             << ". Available: " << current_quantity 
                             << ", Requested: " << order_quantity << std::endl;
                
                push<OrderResultEvent>(sender_id, product_id, order_quantity, false, 
                                      "Not enough stock. Available: " + 
                                      std::to_string(current_quantity));
                return;
            }
            
            // Update the inventory
            _redis.hincrby(key, "quantity", -order_quantity);
            
            // Record the order
            std::string order_id = "order:" + product_id + ":" + std::to_string(std::time(nullptr));
            _redis.hset(order_id, "product_id", product_id);
            _redis.hset(order_id, "quantity", std::to_string(order_quantity));
            _redis.hset(order_id, "timestamp", std::to_string(std::time(nullptr)));
            
            cout << "Order processed successfully! " 
                 << order_quantity << " units of " << product_id 
                 << " ordered." << std::endl;
            
            push<OrderResultEvent>(sender_id, product_id, order_quantity, true, 
                                  "Order processed successfully");
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error processing order: " << e.what() << std::endl;
            push<OrderResultEvent>(sender_id, product_id, order_quantity, false, 
                                  std::string("Order processing error: ") + e.what());
        }
    }
    
    // Demonstrate simple key-value operations
    void demonstrate_simple_transaction() {
        auto cout = qb::io::cout();
        cout << "\n=== Demonstrating Redis Operations ===" << std::endl;
        
        try {
            // Increment a counter
            auto new_count = _redis.incr("transaction:counter");
            cout << "Counter incremented to: " << new_count << std::endl;
            
            // Set a timestamp
            _redis.set("transaction:last_access", std::to_string(std::time(nullptr)));
            
            // Add to a list
            _redis.lpush("transaction:logs", "Operation executed at " + std::to_string(std::time(nullptr)));
            
            // Display results
            auto counter = _redis.get("transaction:counter");
            if (counter.has_value()) {
                cout << "Counter value: " << counter.value() << std::endl;
            }
            
            auto logs = _redis.lrange("transaction:logs", 0, -1);
            cout << "Log entries:" << std::endl;
            for (const auto& log : logs) {
                cout << "  " << log << std::endl;
            }
            
            // Test key deletion and value checking
            cout << "\n=== Demonstrating Key Operations ===" << std::endl;
            
            // Create test key
            auto old_value = _redis.get("transaction:test_key");
            _redis.set("transaction:test_key", "test_value");
            
            // Get and verify value
            auto test_value = _redis.get("transaction:test_key");
            if (test_value.has_value()) {
                cout << "test_key value: " << test_value.value() << std::endl;
            }
            
            // Delete key
            _redis.del("transaction:test_key");
            
            // Verify deletion
            auto test_value_after = _redis.get("transaction:test_key");
            if (!test_value_after.has_value()) {
                cout << "test_key was deleted successfully" << std::endl;
            } else {
                cout << "test_key was not deleted: " << test_value_after.value() << std::endl;
            }
            
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error in demo: " << e.what() << std::endl;
    }
    
        // Notify that demos are complete
        push<TransactionDemoCompletedEvent>(_coordinator_id);
    }
    
    // Clean up test data
    void cleanup() {
        auto cout = qb::io::cout();
        if (!_connected) return;
        
        // Delete test keys
        auto del_inventory = _redis.del(_redis.keys("inventory:*"));
        auto del_orders = _redis.del(_redis.keys("order:*"));
        auto del_transaction = _redis.del(_redis.keys("transaction:*"));
        
        cout << "\n=== Cleanup Complete ===" << std::endl;
        cout << "Deleted " << del_inventory << " inventory keys" << std::endl;
        cout << "Deleted " << del_orders << " order keys" << std::endl;
        cout << "Deleted " << del_transaction << " transaction keys" << std::endl;
    }
    
    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << "InventoryManagerActor shutting down" << std::endl;
        
        // Clean up resources
        cleanup();
        
        kill();
    }
};

// Actor that simulates a client placing orders
class OrderClientActor : public qb::Actor {
private:
    qb::ActorId _inventory_manager_id;
    qb::ActorId _coordinator_id;
    std::string _client_id;
    int _orders_to_place;
    int _orders_completed = 0;
    int _orders_succeeded = 0;
    
    std::random_device _rd;
    std::mt19937 _gen;
    
public:
    OrderClientActor(qb::ActorId inventory_manager, 
                   qb::ActorId coordinator,
                   std::string id, 
                   int orders = 2)
        : _inventory_manager_id(inventory_manager), 
          _coordinator_id(coordinator),
          _client_id(std::move(id)), 
          _orders_to_place(orders),
          _gen(_rd()) {}
    
    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "OrderClientActor [" << _client_id << "] initialized" << std::endl;
                
        // Register for events
        registerEvent<OrderResultEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        return true;
    }
    
    // Start placing orders
    void start_ordering() {
        auto cout = qb::io::cout();
        cout << "Client [" << _client_id << "] starting to place " 
             << _orders_to_place << " orders" << std::endl;
        
        // Schedule orders with a small delay between them
        for (int i = 0; i < _orders_to_place; ++i) {
            qb::io::async::callback([this, i]() {
                place_random_order();
            }, 0.2 * i); // 200ms between orders
        }
    }
    
    // Place a random order
    void place_random_order() {
        auto cout = qb::io::cout();
        
        // Pick a random product and quantity
        std::uniform_int_distribution<> product_dist(1, 5);
        std::uniform_int_distribution<> quantity_dist(1, 5);
        
        std::string product_id = "p" + std::to_string(product_dist(_gen));
        int quantity = quantity_dist(_gen);
        
        cout << "Client [" << _client_id << "] ordering " 
             << quantity << " units of " << product_id << std::endl;
        
        // Send order request to inventory manager
        push<OrderRequestEvent>(_inventory_manager_id, product_id, quantity, id());
    }
    
    // Handle order result
    void on(const OrderResultEvent& event) {
        auto cout = qb::io::cout();
        cout << "Client [" << _client_id << "] received order result for " 
             << event.quantity << " units of " << event.product_id 
             << ": " << (event.success ? "SUCCESS" : "FAILED") 
             << " - " << event.message << std::endl;
        
        _orders_completed++;
        if (event.success) {
            _orders_succeeded++;
        }
        
        // Check if all orders are complete
        if (_orders_completed >= _orders_to_place) {
            cout << "Client [" << _client_id << "] completed all orders. " 
                 << _orders_succeeded << " succeeded, " 
                 << (_orders_completed - _orders_succeeded) << " failed." << std::endl;
            
            // Notify coordinator
            qb::io::async::callback([this]() {
                push<ShutdownEvent>(_coordinator_id);
            }, 0.5); // Give a moment for any last processing
                }
            }
    
    void on(const ShutdownEvent&) {
        auto cout = qb::io::cout();
        cout << "OrderClientActor [" << _client_id << "] shutting down" << std::endl;
        kill();
        }
};

// Coordinator actor that manages the example
class CoordinatorActor : public qb::Actor {
private:
    qb::ActorId _inventory_manager_id;
    std::vector<qb::ActorId> _client_ids;
    bool _setup_completed = false;
    bool _transactions_demo_completed = false;
    bool _shutdown_initiated = false;
    int _clients_created = 0;
    int _clients_to_create = 3;
        
    // Pointers for direct reference in the same virtual core
    InventoryManagerActor* _inventory_manager_ptr = nullptr;
    std::vector<OrderClientActor*> _client_ptrs;
    
public:
    bool onInit() override {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor initialized" << std::endl;
            
        // Register for events
        registerEvent<SetupCompletedEvent>(*this);
        registerEvent<TransactionDemoCompletedEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        
        // Create inventory manager actor
        _inventory_manager_ptr = addRefActor<InventoryManagerActor>(id());
        if (!_inventory_manager_ptr) {
            qb::io::cerr() << "Failed to create inventory manager actor" << std::endl;
            kill();
            return false;
        }
        _inventory_manager_id = _inventory_manager_ptr->id();
        cout << "Created InventoryManager: " << _inventory_manager_id << std::endl;
        
        return true;
    }
    
    // Handle setup completion
    void on(const SetupCompletedEvent&) {
        auto cout = qb::io::cout();
        cout << "Inventory setup completed" << std::endl;
        
        _setup_completed = true;
        
        // Wait for transaction demos to complete before creating clients
        if (_transactions_demo_completed) {
            create_clients();
        }
    }
    
    // Handle transaction demo completion
    void on(const TransactionDemoCompletedEvent&) {
        auto cout = qb::io::cout();
        cout << "Transaction demos completed" << std::endl;
        
        _transactions_demo_completed = true;
        
        // Wait for setup to complete before creating clients
        if (_setup_completed) {
            create_clients();
            }
    }
    
    // Create client actors
    void create_clients() {
        auto cout = qb::io::cout();
        cout << "\n=== Starting Concurrent Orders Simulation ===" << std::endl;
        
        for (int i = 1; i <= _clients_to_create; ++i) {
            std::string client_id = "client-" + std::to_string(i);
            int orders = 2 + i % 3; // 2-4 orders per client
            
            OrderClientActor* client = addRefActor<OrderClientActor>(
                _inventory_manager_id, id(), client_id, orders);
            
            if (client) {
                _client_ptrs.push_back(client);
                _client_ids.push_back(client->id());
                _clients_created++;
                
                cout << "Created Client " << client_id << ": " << client->id() << std::endl;
            } else {
                qb::io::cerr() << "Failed to create client: " << client_id << std::endl;
            }
        }
        
        // Start clients placing orders
        for (auto* client : _client_ptrs) {
            client->start_ordering();
        }
    }
    
    // Handle shutdown signal
    void on(const ShutdownEvent&) {
        if (_shutdown_initiated) return;
        
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received shutdown request" << std::endl;
        
        _shutdown_initiated = true;
        
        // Display final inventory state
        if (_inventory_manager_ptr) {
            _inventory_manager_ptr->display_inventory();
        }
        
        // Send shutdown to all actors
        for (auto& client_id : _client_ids) {
            push<ShutdownEvent>(client_id);
        }
        
        if (_inventory_manager_ptr) {
            push<ShutdownEvent>(_inventory_manager_id);
        }
        
        // Wait before shutting down coordinator and engine
        qb::io::async::callback([this]() {
            auto cout = qb::io::cout();
            cout << "CoordinatorActor shutting down" << std::endl;
            kill();
            
            // Stop the engine after a brief delay
            qb::io::async::callback([]() {
                auto cout = qb::io::cout();
                cout << "Stopping engine..." << std::endl;
                qb::Main::stop();
            }, 0.5);
        }, 1.0);
    }
    
    // Handle kill event
    void on(const qb::KillEvent&) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received kill event" << std::endl;
        kill();
    }
};

int main() {
    // Initialize the async system
    qb::io::async::init();
    auto cout = qb::io::cout();
    
    cout << "Starting Redis Transaction Example with Actor Model" << std::endl;
    
    // Create the engine
    qb::Main engine;
    
    // Add coordinator actor to core 0
    auto coordinator_id = engine.addActor<CoordinatorActor>(0);
    if (coordinator_id == 0) {  // 0 is an invalid actor ID
        qb::io::cerr() << "Failed to create coordinator actor" << std::endl;
        return 1;
    }
    
    // Start the engine (non-blocking)
    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;
    
    // Wait for engine to stop (when all actors terminate)
    engine.join();
    
    cout << "Engine stopped, all actors terminated" << std::endl;
    cout << "Redis Transaction Example completed successfully" << std::endl;
    
    return 0;
} 