/**
 * @file examples/coroutine/actor_example.cpp
 * @brief Actor with coroutine example
 *
 * This example demonstrates using coroutines within QB Actors.
 * It shows the proper pattern for async I/O while maintaining Actor safety.
 *
 * Build:
 *   cmake -B build -DQB_BUILD_EXAMPLES=ON
 *   cmake --build build --target actor_coroutine_example
 *
 * Run:
 *   ./build/examples/coroutine/actor_example
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async/coroutine.h>
#include <iostream>
#include <chrono>

// Events for our example
struct StartProcessing : qb::Event {
    int request_id;
    std::string data;

    StartProcessing(int id, std::string d)
        : request_id(id), data(std::move(d)) {}
};

struct ProcessingComplete : qb::Event {
    int request_id;
    std::string result;
    uint64_t processing_time_ns;

    ProcessingComplete(int id, std::string r, uint64_t time)
        : request_id(id), result(std::move(r)), processing_time_ns(time) {}
};

// Simulated async service (could be Redis, HTTP, database, etc.)
class AsyncService {
public:
    // Simulated async operation
    static qb::io::async::task<std::string> process_data(const std::string& input) {
        // Simulate network/database delay
        co_await qb::io::async::sleep(std::chrono::milliseconds(100));

        // Process the data
        std::string result = "Processed: " + input;
        co_return result;
    }
};

// Actor that uses coroutines for async I/O
class CoroWorker : public qb::Actor {
    int processed_count_ = 0;

public:
    qb::io::async::task<bool> onInit() override {
        std::cout << "CoroWorker initialized with ID: " << id() << "\n";

        registerEvent<StartProcessing>(*this);
        registerEvent<ProcessingComplete>(*this);

        co_return true;
    }

    // Synchronous event handler - this is where we receive requests
    void on(StartProcessing& req) {
        auto start_time = time();

        std::cout << "[Actor " << id() << "] Received request "
                  << req.request_id << " with data: " << req.data << "\n";

        // ⚠️ CRITICAL: Capture by VALUE only!
        int req_id = req.request_id;
        std::string data = req.data;
        uint64_t start = start_time;

        // Launch async coroutine scoped to this actor's lifetime
        spawn([req_id, data, start](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            // This runs in isolated context - we can only use ctx interface
            // NO direct access to Actor members here!

            // Perform async I/O
            std::string result = co_await AsyncService::process_data(data);

            uint64_t elapsed = ctx.time() - start;

            // Return result via event (only way to communicate back)
            ctx.template push<ProcessingComplete>(req_id, result, elapsed);

            std::cout << "[Coroutine] Request " << req_id << " completed in "
                      << elapsed / 1'000'000 << "ms\n";
        });

        std::cout << "[Actor " << id() << "] Spawned coroutine for request "
                  << req.request_id << "\n";
    }

    // Synchronous event handler - this is where we receive async results
    void on(ProcessingComplete& ev) {
        // We're back in safe Actor context with exclusive state access
        ++processed_count_;

        std::cout << "[Actor " << id() << "] Request " << ev.request_id
                  << " completed. Result: " << ev.result
                  << " (took " << ev.processing_time_ns / 1'000'000 << "ms)\n";

        std::cout << "[Actor " << id() << "] Total processed: "
                  << processed_count_ << "\n";

        // Reply to the original sender if needed
        // reply(ev);  // Uncomment if we want to send back to caller
    }

    void on(qb::KillEvent const& ev) {
        std::cout << "[Actor " << id() << "] Shutting down. Total processed: "
                  << processed_count_ << "\n";
        kill();
    }
};

// Driver actor that sends test requests to CoroWorker
class TestDriver : public qb::Actor {
    qb::ActorId _worker_id;

public:
    explicit TestDriver(qb::ActorId worker_id)
        : _worker_id(worker_id) {}

    qb::io::async::task<bool> onInit() override {
        std::cout << "TestDriver initialized\n";
        // Send a few test requests immediately
        std::cout << "\nSending test requests...\n\n";
        for (int i = 1; i <= 3; ++i) {
            push<StartProcessing>(_worker_id, i, "TestData-" + std::to_string(i));
        }
        // Wait 1 second then broadcast shutdown (enough time for 100ms processing × 3)
        co_await context().sleep(std::chrono::seconds(1));
        std::cout << "\nInitiating shutdown...\n";
        broadcast<qb::KillEvent>();
        co_return true;
    }
};

// Main function for the example
int main(int argc, char* argv[]) {
    std::cout << "=== QB Actor + Coroutine Example ===\n\n";

    // Create the main engine
    qb::Main engine;

    // Add our coroutine worker actor on core 0
    auto worker_id = engine.addActor<CoroWorker>(0);

    // Add the test driver on core 0, passing the worker's id
    engine.addActor<TestDriver>(0, worker_id);

    // Start the engine
    std::cout << "Starting QB engine...\n\n";

    engine.start();
    engine.join();

    std::cout << "\n=== Example completed! ===\n";

    return 0;
}
