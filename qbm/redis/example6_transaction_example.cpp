/**
 * @file examples/qbm/redis/example6_transaction_example.cpp
 * @example qbm-redis: Transactions and Atomic Operations with Actors (Coroutine API)
 *
 * @brief This example demonstrates concepts related to Redis transactions and atomic
 * operations within a QB actor system, simulating a simple inventory management
 * scenario.  It uses the modern coroutine API throughout.
 *
 * @details
 * The system includes several actors:
 * 1.  `InventoryManagerActor`:
 *     -   Connects to Redis via `co_await _redis.connect()` in its `onInit()` coroutine.
 *     -   `setup_inventory()` is spawned from onInit after connect: populates Redis with
 *         initial product data using `co_await _redis.hset()` etc.
 *     -   `on(const OrderRequestEvent&)` spawns a coroutine to:
 *         -   `co_await _redis.hget(key, "quantity")` for current stock.
 *         -   Check availability, then `co_await _redis.hincrby()` to decrement.
 *         -   `co_await _redis.hset()` to record the order.
 *     -   `demonstrate_redis_operations()` shows INCR, SET, LPUSH, GET, LRANGE, DEL.
 *     -   `on(const ShutdownEvent&)` spawns cleanup (del inventory/order keys) then kills.
 * 2.  `OrderClientActor` (multiple instances):
 *     -   Simulates a client placing orders.  Each order is a spawned coroutine with a
 *         scheduled delay between orders.
 *     -   Receives `OrderResultEvent`s and logs the outcome.
 * 3.  `CoordinatorActor`:
 *     -   Manages the overall simulation lifecycle.
 *     -   Waits for setup and demo completion before starting clients.
 *     -   Orchestrates graceful shutdown.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::io::async::task<bool>` onInit() and spawned coroutines.
 * - `co_await client.connect()` and `co_await client.<command>()`.
 * - `qb::redis::Reply<T>`: `ok()` and `result()`.
 * - Hash commands: `hset`, `hget`, `hgetall`, `hincrby`.
 * - Key commands: `keys`, `del`.
 * - String commands: `set`, `get`, `incr`, `setex`.
 * - List commands: `lpush`, `lrange`.
 * - Waiting without outliving the actor: `spawn(...)` + `co_await ctx.sleep(d)` + a
 *   self-addressed tick event, and the one case where a bare `qb::io::async::callback(fn, d)`
 *   is still correct — see the comment on `DemoOperationsTick` and
 *   `CoordinatorActor::on(CoordinatorShutdownTick&)`.
 * - `addRefActor` returning `qb::ActorHandle<T>` with `.valid()` / `.id()`.
 */

#include <iomanip>
#include <iostream>
#include <random>
#include <string_view>
#include <qbm/redis/redis.h>
#include <string>
#include <vector>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>
#include <qb/string.h>
#include <qb/system/parse.h>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// Structure to represent a product in inventory
struct Product {
    std::string id;
    std::string name;
    int         price;
    int         quantity;
};

// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. This is NOT a cross-core-only
// concern: pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr`.
//
// Event to request an order
struct OrderRequestEvent : qb::Event {
    qb::string<64> product_id;
    int            quantity;
    qb::ActorId    sender_id;

    OrderRequestEvent(std::string_view id, int qty, qb::ActorId sender)
        : product_id(id)
        , quantity(qty)
        , sender_id(sender) {}
};

// Event to report order result
struct OrderResultEvent : qb::Event {
    qb::string<64>  product_id;
    int             quantity;
    bool            success;
    qb::string<128> message;

    OrderResultEvent(std::string_view id, int qty, bool succ, std::string_view msg)
        : product_id(id)
        , quantity(qty)
        , success(succ)
        , message(msg) {}
};

// Event to signal that inventory setup is complete
struct SetupCompletedEvent : qb::Event {
    explicit SetupCompletedEvent() {}
};

// Event to signal that operations demo is complete
struct TransactionDemoCompletedEvent : qb::Event {
    explicit TransactionDemoCompletedEvent() {}
};

/**
 * @brief Self-addressed wake-ups, one per delay in this example.
 *
 * Every one of these replaces a `qb::io::async::callback([this]{ ... }, d)`. That overload
 * heap-allocates a `Timeout` owned by the event loop, not by the actor, so nothing cancels it
 * when the actor is killed and it fires against a destroyed object. That is not theoretical
 * here: `OrderClientActor::on(OrderResultEvent&)` used to schedule its shutdown notification
 * that way, and AddressSanitizer reported `heap-use-after-free` in the lambda on every run of
 * this example. The replacement is `spawn(...)` + `co_await ctx.sleep(d)` — a wait the actor's
 * cancellation scope owns — plus one of these events to do the work back on the actor.
 */
struct DemoOperationsTick : qb::Event {};      ///< 1 s after setup: run the extra Redis demo
struct PlaceOrderTick : qb::Event {};          ///< staggered: place one random order
struct NotifyCoordinatorTick : qb::Event {};   ///< 500 ms after the last result: tell the coordinator
struct CoordinatorShutdownTick : qb::Event {}; ///< 1 s after shutdown starts: leave

// Event to signal shutdown
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

// Helper function to generate example products
std::vector<Product>
initialize_inventory() {
    return {
        {"p1", "Laptop", 999, 10},
        {"p2", "Smartphone", 699, 20},
        {"p3", "Headphones", 99, 50},
        {"p4", "Monitor", 299, 15},
        {"p5", "Keyboard", 59, 30}
    };
}

// Actor for managing inventory with Redis
class InventoryManagerActor : public qb::Actor {
private:
    qb::redis::tcp::client _redis{REDIS_URI};
    qb::ActorId            _coordinator_id;

public:
    explicit InventoryManagerActor(qb::ActorId coordinator)
        : _coordinator_id(coordinator) {}

    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "InventoryManagerActor initialized" << std::endl;

        // Register for events before the first co_await
        registerEvent<OrderRequestEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<DemoOperationsTick>(*this);

        if (!co_await _redis.connect()) {
            qb::io::cerr() << "Failed to connect to Redis" << std::endl;
            co_return false;
        }

        cout << "Connected to Redis successfully!" << std::endl;

        // Spawn the inventory-setup coroutine — runs concurrently after activation.
        //
        // `setup_inventory()` is a member coroutine: its implicit `this` is the actor, and it
        // touches members after every `co_await _redis...`. That is safe by OWNERSHIP, not by
        // `spawn`'s scope — a qbm command awaiter registers nothing with the cancellation
        // token, so `kill()` never reaches it. `_redis` is a MEMBER, so `~Actor` destroys the
        // client with its pending-reply queue, the reply callback is discarded UNINVOKED, and
        // the coroutine never resumes (measured: an actor killed while parked on a 3 s BRPOP
        // never resumes, ASan silent). The cost is an orphaned frame, not a use-after-free.
        // The same body over a client that outlives the actor WOULD be a use-after-free.
        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> { co_await setup_inventory(); });

        co_return true;
    }

    // Setup inventory with initial products
    qb::io::async::task<void>
    setup_inventory() {
        auto cout = qb::io::cout();

        // Clean up any existing inventory/order keys
        auto inv_keys = co_await _redis.keys("inventory:*");
        if (inv_keys.ok() && !inv_keys.result().empty()) {
            [[maybe_unused]] auto d1 = co_await _redis.del(inv_keys.result());
        }
        auto ord_keys = co_await _redis.keys("order:*");
        if (ord_keys.ok() && !ord_keys.result().empty()) {
            [[maybe_unused]] auto d2 = co_await _redis.del(ord_keys.result());
        }

        auto products = initialize_inventory();

        for (const auto &p : products) {
            std::string           key = "inventory:" + p.id;
            [[maybe_unused]] auto s1  = co_await _redis.hset(key, "name", p.name);
            [[maybe_unused]] auto s2  = co_await _redis.hset(key, "price", std::to_string(p.price));
            [[maybe_unused]] auto s3  = co_await _redis.hset(key, "quantity", std::to_string(p.quantity));
        }

        cout << "Inventory initialized with " << products.size() << " products" << std::endl;

        co_await display_inventory();

        // Notify coordinator that setup is complete
        push<SetupCompletedEvent>(_coordinator_id);

        // Demonstrate other Redis operations after a brief pause. The pause is a coroutine this
        // actor's cancellation scope owns; the work restarts from `on(DemoOperationsTick&)`.
        scheduleTick<DemoOperationsTick>(std::chrono::seconds(1));
    }

    void
    on(const DemoOperationsTick &) {
        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> { co_await demonstrate_redis_operations(); });
    }

    /**
     * @brief Sleep `d`, then wake this actor with a `TickEvent`
     *
     * See the comment on `DemoOperationsTick` for why this exists rather than
     * `qb::io::async::callback([this]{ ... }, d)`.
     */
    template <typename TickEvent>
    void
    scheduleTick(qb::duration d) {
        spawn([d](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(d);
            ctx.template push<TickEvent>();
        });
    }

    // Display current inventory
    qb::io::async::task<void>
    display_inventory() {
        auto cout = qb::io::cout();

        auto keys_r = co_await _redis.keys("inventory:*");
        if (!keys_r.ok() || keys_r.result().empty()) {
            cout << "No products in inventory!" << std::endl;
            co_return;
        }

        cout << "\n=== Current Inventory ===" << std::endl;
        cout << std::setw(10) << "ID" << std::setw(15) << "Name" << std::setw(10) << "Price" << std::setw(10) << "Quantity" << std::endl;
        cout << std::string(45, '-') << std::endl;

        for (const auto &key : keys_r.result()) {
            auto data_r = co_await _redis.hgetall(key);
            if (!data_r.ok())
                continue;
            const auto &m = data_r.result();

            std::string id       = key.substr(key.find(':') + 1);
            std::string name     = m.count("name") ? m.at("name") : "?";
            std::string price    = m.count("price") ? m.at("price") : "?";
            std::string quantity = m.count("quantity") ? m.at("quantity") : "?";

            cout << std::setw(10) << id << std::setw(15) << name << std::setw(10) << price << std::setw(10) << quantity << std::endl;
        }
        cout << std::endl;
    }

    // Process an order
    void
    on(const OrderRequestEvent &event) {
        std::string product_id = event.product_id.c_str();
        int         order_qty  = event.quantity;
        qb::ActorId sender_id  = event.sender_id;

        spawn([this, product_id, order_qty, sender_id](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto        cout = qb::io::cout();
            std::string key  = "inventory:" + product_id;

            // Get current quantity
            auto qty_r = co_await _redis.hget(key, "quantity");
            if (!qty_r.ok() || !qty_r.result().has_value()) {
                push<OrderResultEvent>(sender_id, product_id, order_qty, false, "Product not found");
                co_return;
            }

            // The quantity field is read back from Redis; tolerate a malformed
            // value instead of throwing — a bad parse yields 0 (out of stock).
            int current_qty = qb::to_number<int>(*qty_r.result()).value_or(0);

            if (current_qty < order_qty) {
                cout << "Not enough stock for " << product_id << ". Available: " << current_qty << ", Requested: " << order_qty << std::endl;

                push<OrderResultEvent>(sender_id, product_id, order_qty, false, "Not enough stock. Available: " + std::to_string(current_qty));
                co_return;
            }

            // Decrement inventory
            [[maybe_unused]] auto dq = co_await _redis.hincrby(key, "quantity", -order_qty);

            // Record the order
            std::string           order_id = "order:" + product_id + ":" + std::to_string(std::time(nullptr));
            [[maybe_unused]] auto oh1      = co_await _redis.hset(order_id, "product_id", product_id);
            [[maybe_unused]] auto oh2      = co_await _redis.hset(order_id, "quantity", std::to_string(order_qty));
            [[maybe_unused]] auto oh3      = co_await _redis.hset(order_id, "timestamp", std::to_string(std::time(nullptr)));

            cout << "Order processed successfully! " << order_qty << " units of " << product_id << " ordered." << std::endl;

            push<OrderResultEvent>(sender_id, product_id, order_qty, true, "Order processed successfully");
        });
    }

    // Demonstrate simple Redis key-value operations
    qb::io::async::task<void>
    demonstrate_redis_operations() {
        auto cout = qb::io::cout();
        cout << "\n=== Demonstrating Redis Operations ===" << std::endl;

        // Increment a counter
        auto ctr_r = co_await _redis.incr("transaction:counter");
        if (ctr_r.ok()) {
            cout << "Counter incremented to: " << ctr_r.result() << std::endl;
        }

        // Set a timestamp
        [[maybe_unused]] auto ts = co_await _redis.set("transaction:last_access", std::to_string(std::time(nullptr)));

        // Add to a list
        [[maybe_unused]] auto lp = co_await _redis.lpush("transaction:logs", "Operation executed at " + std::to_string(std::time(nullptr)));

        // Display counter value
        auto get_r = co_await _redis.get("transaction:counter");
        if (get_r.ok() && get_r.result().has_value()) {
            cout << "Counter value: " << *get_r.result() << std::endl;
        }

        // Display log entries
        auto logs_r = co_await _redis.lrange("transaction:logs", 0, -1);
        if (logs_r.ok()) {
            cout << "Log entries:" << std::endl;
            for (const auto &log : logs_r.result()) {
                cout << "  " << log << std::endl;
            }
        }

        cout << "\n=== Demonstrating Key Operations ===" << std::endl;

        // Set a test key
        [[maybe_unused]] auto stk = co_await _redis.set("transaction:test_key", "test_value");

        auto test_r = co_await _redis.get("transaction:test_key");
        if (test_r.ok() && test_r.result().has_value()) {
            cout << "test_key value: " << *test_r.result() << std::endl;
        }

        // Delete the test key
        [[maybe_unused]] auto dtk = co_await _redis.del("transaction:test_key");

        auto after_r = co_await _redis.get("transaction:test_key");
        if (after_r.ok() && !after_r.result().has_value()) {
            cout << "test_key was deleted successfully" << std::endl;
        }

        // Notify coordinator
        push<TransactionDemoCompletedEvent>(_coordinator_id);
    }

    // Cleanup all test data
    qb::io::async::task<void>
    cleanup() {
        auto cout = qb::io::cout();

        auto      inv_keys = co_await _redis.keys("inventory:*");
        long long del_inv  = 0;
        if (inv_keys.ok() && !inv_keys.result().empty()) {
            auto r = co_await _redis.del(inv_keys.result());
            if (r.ok())
                del_inv = r.result();
        }

        auto      ord_keys = co_await _redis.keys("order:*");
        long long del_ord  = 0;
        if (ord_keys.ok() && !ord_keys.result().empty()) {
            auto r = co_await _redis.del(ord_keys.result());
            if (r.ok())
                del_ord = r.result();
        }

        auto      txn_keys = co_await _redis.keys("transaction:*");
        long long del_txn  = 0;
        if (txn_keys.ok() && !txn_keys.result().empty()) {
            auto r = co_await _redis.del(txn_keys.result());
            if (r.ok())
                del_txn = r.result();
        }

        cout << "\n=== Cleanup Complete ===" << std::endl;
        cout << "Deleted " << del_inv << " inventory keys" << std::endl;
        cout << "Deleted " << del_ord << " order keys" << std::endl;
        cout << "Deleted " << del_txn << " transaction keys" << std::endl;
    }

    void
    on(const ShutdownEvent &) {
        auto cout = qb::io::cout();
        cout << "InventoryManagerActor shutting down" << std::endl;

        spawn([this](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            co_await cleanup();
            kill();
        });
    }
};

// Actor that simulates a client placing orders
class OrderClientActor : public qb::Actor {
private:
    qb::ActorId _inventory_manager_id;
    qb::ActorId _coordinator_id;
    std::string _client_id;
    int         _orders_to_place;
    int         _orders_completed = 0;
    int         _orders_succeeded = 0;

    std::random_device _rd;
    std::mt19937       _gen;

public:
    OrderClientActor(qb::ActorId inventory_manager, qb::ActorId coordinator, std::string id, int orders = 2)
        : _inventory_manager_id(inventory_manager)
        , _coordinator_id(coordinator)
        , _client_id(std::move(id))
        , _orders_to_place(orders)
        , _gen(_rd()) {}

    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "OrderClientActor [" << _client_id << "] initialized" << std::endl;

        registerEvent<OrderResultEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<PlaceOrderTick>(*this);
        registerEvent<NotifyCoordinatorTick>(*this);

        co_return true;
    }

    // Start placing orders (called by CoordinatorActor directly)
    void
    start_ordering() {
        auto cout = qb::io::cout();
        cout << "Client [" << _client_id << "] starting to place " << _orders_to_place << " orders" << std::endl;

        // One coroutine paces the whole batch (0 ms, 200 ms, 400 ms, ...) instead of N loop-owned
        // timers each holding a raw `this`. `_orders_to_place` is read HERE, on the actor, and
        // captured by value; the coroutine touches nothing but its own frame and `ctx`.
        spawn([n = _orders_to_place](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            for (int i = 0; i < n; ++i) {
                if (i)
                    co_await ctx.sleep(std::chrono::milliseconds(200));
                ctx.template push<PlaceOrderTick>();
            }
        });
    }

    void
    on(const PlaceOrderTick &) {
        place_random_order();
    }

    void
    on(const NotifyCoordinatorTick &) {
        push<ShutdownEvent>(_coordinator_id);
    }

    void
    place_random_order() {
        auto cout = qb::io::cout();

        std::uniform_int_distribution<> product_dist(1, 5);
        std::uniform_int_distribution<> quantity_dist(1, 5);

        std::string product_id = "p" + std::to_string(product_dist(_gen));
        int         quantity   = quantity_dist(_gen);

        cout << "Client [" << _client_id << "] ordering " << quantity << " units of " << product_id << std::endl;

        push<OrderRequestEvent>(_inventory_manager_id, product_id, quantity, id());
    }

    void
    on(const OrderResultEvent &event) {
        auto cout = qb::io::cout();
        cout << "Client [" << _client_id << "] received order result for " << event.quantity << " units of " << event.product_id << ": "
             << (event.success ? "SUCCESS" : "FAILED") << " - " << event.message << std::endl;

        _orders_completed++;
        if (event.success)
            _orders_succeeded++;

        if (_orders_completed >= _orders_to_place) {
            cout << "Client [" << _client_id << "] completed all orders. " << _orders_succeeded << " succeeded, "
                 << (_orders_completed - _orders_succeeded) << " failed." << std::endl;

            // THIS is the site AddressSanitizer convicted, three runs of three: the lambda's
            // `push<ShutdownEvent>(_coordinator_id)` read `this` and `_coordinator_id` half a
            // second after the coordinator had already killed this client. `heap-use-after-free`
            // at `example6_transaction_example.cpp`, in `qb::Actor::push`, reading 4 bytes 76
            // bytes into the freed actor. The wait now belongs to the actor's cancellation scope
            // and the `push` happens in `on(NotifyCoordinatorTick&)`, on a live actor or not at all.
            spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
                co_await ctx.sleep(std::chrono::milliseconds(500));
                ctx.template push<NotifyCoordinatorTick>();
            });
        }
    }

    void
    on(const ShutdownEvent &) {
        auto cout = qb::io::cout();
        cout << "OrderClientActor [" << _client_id << "] shutting down" << std::endl;
        kill();
    }
};

// Coordinator actor that manages the example
class CoordinatorActor : public qb::Actor {
private:
    qb::ActorId              _inventory_manager_id;
    std::vector<qb::ActorId> _client_ids;

    bool _setup_completed             = false;
    bool _transactions_demo_completed = false;
    bool _shutdown_initiated          = false;

    int _clients_to_create = 3;

    // Keep handles alive (ActorHandle<T> is copyable/movable but not a raw ptr)
    std::vector<qb::ActorHandle<OrderClientActor>> _client_handles;
    qb::ActorHandle<InventoryManagerActor>         _inventory_manager_handle;

public:
    qb::io::async::task<bool>
    onInit() override {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor initialized" << std::endl;

        registerEvent<SetupCompletedEvent>(*this);
        registerEvent<TransactionDemoCompletedEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<CoordinatorShutdownTick>(*this);

        // Create inventory manager actor
        _inventory_manager_handle = addRefActor<InventoryManagerActor>(id());
        if (!_inventory_manager_handle.valid()) {
            qb::io::cerr() << "Failed to create inventory manager actor" << std::endl;
            co_return false;
        }
        _inventory_manager_id = _inventory_manager_handle.id();
        cout << "Created InventoryManager: " << _inventory_manager_id << std::endl;

        co_return true;
    }

    void
    on(const SetupCompletedEvent &) {
        auto cout = qb::io::cout();
        cout << "Inventory setup completed" << std::endl;

        _setup_completed = true;

        if (_transactions_demo_completed) {
            create_clients();
        }
    }

    void
    on(const TransactionDemoCompletedEvent &) {
        auto cout = qb::io::cout();
        cout << "Transaction demos completed" << std::endl;

        _transactions_demo_completed = true;

        if (_setup_completed) {
            create_clients();
        }
    }

    void
    create_clients() {
        auto cout = qb::io::cout();
        cout << "\n=== Starting Concurrent Orders Simulation ===" << std::endl;

        for (int i = 1; i <= _clients_to_create; ++i) {
            std::string client_id = "client-" + std::to_string(i);
            int         orders    = 2 + i % 3; // 2-4 orders per client

            auto h = addRefActor<OrderClientActor>(_inventory_manager_id, id(), client_id, orders);

            if (h.valid()) {
                _client_ids.push_back(h.id());
                _client_handles.push_back(h);
                cout << "Created Client " << client_id << ": " << h.id() << std::endl;
            } else {
                qb::io::cerr() << "Failed to create client: " << client_id << std::endl;
            }
        }

        // Start clients placing orders
        for (auto &h : _client_handles) {
            if (auto *p = h.get())
                p->start_ordering();
        }
    }

    void
    on(const ShutdownEvent &) {
        if (_shutdown_initiated)
            return;

        auto cout = qb::io::cout();
        cout << "CoordinatorActor received shutdown request" << std::endl;

        _shutdown_initiated = true;

        // Display final inventory state through the handle
        if (_inventory_manager_handle.get()) {
            // Spawn display in the manager's context via a message instead
            // (display_inventory is a coroutine on the manager side)
        }

        // Send shutdown to all actors
        for (auto &client_id : _client_ids) {
            push<ShutdownEvent>(client_id);
        }
        push<ShutdownEvent>(_inventory_manager_id);

        // Give everyone a second to finish, then leave. `kill()` runs on the actor, so the wait
        // has to come back as an event rather than as a timer holding `this`.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(1));
            ctx.template push<CoordinatorShutdownTick>();
        });
    }

    void
    on(const CoordinatorShutdownTick &) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor shutting down" << std::endl;
        kill();

        // This one MUST stay a bare `qb::io::async::callback(fn, d)`: it captures NOTHING, it
        // stops the engine rather than touching an actor, and it is supposed to outlive every
        // actor including this one. A `spawn(...)` here would be cancelled by the `kill()` above
        // and the engine would never stop.
        qb::io::async::callback(
            []() {
                auto cout = qb::io::cout();
                cout << "Stopping engine..." << std::endl;
                qb::Main::stop();
            },
            std::chrono::milliseconds(500));
    }

    void
    on(const qb::KillEvent &) {
        auto cout = qb::io::cout();
        cout << "CoordinatorActor received kill event" << std::endl;
        kill();
    }
};

int
main() {
    qb::io::async::init();
    auto cout = qb::io::cout();

    cout << "Starting Redis Transaction Example with Actor Model" << std::endl;

    qb::Main engine;

    auto coordinator_id = engine.addActor<CoordinatorActor>(0);
    if (coordinator_id == 0) {
        qb::io::cerr() << "Failed to create coordinator actor" << std::endl;
        return 1;
    }

    engine.start(true);
    cout << "Engine started, actors running..." << std::endl;

    engine.join();

    cout << "Engine stopped, all actors terminated" << std::endl;
    cout << "Redis Transaction Example completed successfully" << std::endl;

    return 0;
}
