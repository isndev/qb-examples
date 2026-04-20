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
    bool onInit() override {
        std::cout << "CoroWorker initialized with ID: " << id() << "\n";

        registerEvent<StartProcessing>(*this);
        registerEvent<ProcessingComplete>(*this);

        return true;
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
        qb::ActorId sender = req.sender;

        // Launch async coroutine in isolated context
        spawn_async([this, req_id, data, start, sender](auto ctx) -> qb::io::async::task<void> {
            // This runs in isolated context - we can only use ctx interface
            // NO direct access to Actor members here!

            // Perform async I/O
            std::string result = co_await AsyncService::process_data(data);

            uint64_t elapsed = ctx.time() - start;

            // Return result via event (only way to communicate back)
            ctx.push<ProcessingComplete>(req_id, result, elapsed);

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

    void on(qb::KillEvent&) override {
        std::cout << "[Actor " << id() << "] Shutting down. Total processed: "
                  << processed_count_ << "\n";
        kill();
    }
};

// Main function for the example
int main(int argc, char* argv[]) {
    std::cout << "=== QB Actor + Coroutine Example ===\n\n";

    // Create the main engine
    qb::Main main;

    // Configure one core
    auto& core = main.core(0);

    // Add our coroutine worker actor
    auto worker_id = core.add<CoroWorker>();

    // Add a test driver that sends requests
    core.add<qb::Actor>([worker_id](qb::Actor& self) {
        bool onInit() override {
            registerEvent<qb::StartEvent>(*this);
            return true;
        }

        void on(qb::StartEvent&) {
            // Send a few test requests
            std::cout << "\nSending test requests...\n\n";

            for (int i = 1; i <= 3; ++i) {
                push<StartProcessing>(worker_id, i, "TestData-" + std::to_string(i));
            }

            // Schedule shutdown after processing completes
            qb::io::async::callback([this]() {
                std::cout << "\nInitiating shutdown...\n";
                broadcast<qb::KillEvent>();
            }, 1.0);  // 1 second delay
        }
    });

    // Start the engine
    std::cout << "Starting QB engine...\n\n";

    if (!main.start()) {
        std::cerr << "Failed to start QB engine\n";
        return 1;
    }

    std::cout << "\n=== Example completed! ===\n";

    return 0;
}
