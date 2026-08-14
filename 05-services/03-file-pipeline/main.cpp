/**
 * @file examples/05-services/03-file-pipeline/main.cpp
 * @tier 05-services
 * @teaches Getting blocking work off the event loop: a manager that owns the queue, a pool of
 *          worker actors spread over the cores that do the file I/O, and a client that drives
 *          the whole run and then shuts it down.
 * @demonstrates qb::Main, qb::Actor, qb::Event, qb::ActorId, addActor<T>, registerEvent<E>,
 *               qb::KillEvent, broadcast<qb::KillEvent>, spawn, qb::ScopedCoroContext,
 *               ctx.sleep, qb::duration, id(), qb::io::cout, qb::io::cerr
 * @prerequisites 01-actors/05-lifecycle, 02-io/02-files
 * @expect "=== QB Core/IO Example: Distributed File Processing ==="
 * @expect "=== All tests completed ==="
 * @expect "System shut down correctly"
 * @example Distributed File Processor - Application Entry Point
 * @brief Main entry point for the distributed file processing example.
 *
 * @details
 * This application demonstrates a system for processing file read and write
 * requests using a manager-worker actor pattern distributed across multiple cores.
 *
 * System Setup:
 * 1.  Initializes the `qb::Main` engine.
 * 2.  Creates a `FileManager` actor on core 0. This actor is responsible for
 *     receiving client requests, queuing them, and dispatching them to available workers.
 * 3.  Creates a pool of `FileWorker` actors (e.g., 4 workers). These workers are
 *     distributed across other available CPU cores (e.g., cores 1, 2, 3, then cycling).
 *     Each `FileWorker` performs the actual file I/O with `qb::io::sys::file`, wrapped in
 *     `qb::io::async::callback(fn)` — the one-argument overload, which runs `fn` INLINE. The
 *     blocking therefore happens on the worker's core, which is the point of the pool.
 * 4.  Creates a `ClientActor` on core 0. This actor simulates a client by sending a
 *     series of test `ReadFileRequest` and `WriteFileRequest` events to the `FileManager`.
 *     It also receives `ReadFileResponse` and `WriteFileResponse` events.
 * 5.  The `ClientActor`, after all its operations are acknowledged, initiates a system-wide
 *     shutdown by broadcasting a `qb::KillEvent`.
 * 6.  The `main` function starts the engine and waits for it to join, indicating all actors
 *     have terminated.
 *
 * This example highlights how to offload potentially blocking I/O operations to worker actors,
 * manage a pool of workers, and queue requests, all within the QB actor model.
 *
 * QB Features Demonstrated:
 * - `qb::Main`: For engine setup and lifecycle.
 * - `qb::Actor`: Base for `FileManager`, `FileWorker`, and `ClientActor`.
 * - `engine.addActor<T>(core_id, ...)`: For multi-core actor deployment.
 * - Custom `qb::Event`s: For requests, responses, and worker status (`messages.h`).
 * - Inter-Actor Communication: `push<Event>(...)` for task dispatch and result forwarding.
 * - The two `qb::io::async::callback` overloads, and why they are not interchangeable:
 *   `FileWorker` uses the one-argument form, which runs its body INLINE; `ClientActor` needs a
 *   real delay and uses `spawn(...)` + `co_await ctx.sleep(d)` + a self-addressed tick event,
 *   because `callback([this]{...}, d)` leaves a timer the actor's death does not cancel.
 * - `qb::io::sys::file`: For synchronous file I/O within `FileWorker`.
 * - Coordinated Shutdown: `ClientActor` broadcasting `qb::KillEvent` after tests.
 * - Manager-Worker Pattern.
 */

#include <qb/main.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <filesystem>
using namespace std::chrono_literals;

#include "file_manager.h"
#include "file_worker.h"
#include "messages.h"

// To simplify namespaces
namespace fs = std::filesystem;
using namespace file_processor;

/**
 * @brief Self-addressed wake-ups for `ClientActor`'s two delays.
 *
 * A delay that must end in a call on the actor is served by `spawn(...)` + `co_await
 * ctx.sleep(d)` and then wakes the actor with one of these. The coroutine itself never touches
 * actor state: it may be suspended when the actor is destroyed, and its sleep is cancelled by
 * the actor's own cancellation scope. Contrast `qb::io::async::callback([this]{...}, d)`, whose
 * timer belongs to the event loop and keeps firing at an actor that is no longer there.
 */
struct StartTestsTick : public qb::Event {}; ///< 500 ms after init: begin the test sequence
struct ShutdownTick : public qb::Event {};   ///< 1 s after the last response: bring the system down

/**
 * @brief Client actor that sends test requests
 */
class ClientActor : public qb::Actor {
private:
    qb::ActorId           _manager_id;
    std::filesystem::path _test_directory;
    uint32_t              _next_request_id  = 1;
    uint32_t              _pending_requests = 0;

public:
    ClientActor(qb::ActorId manager_id, std::filesystem::path test_dir)
        : _manager_id(manager_id)
        , _test_directory(std::move(test_dir)) {
        // Register for response types
        registerEvent<ReadFileResponse>(*this);
        registerEvent<WriteFileResponse>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<StartTestsTick>(*this);
        registerEvent<ShutdownTick>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "ClientActor initialized with ID " << id() << " on core " << id().index() << std::endl;

        // Ensure the test directory exists
        if (!fs::exists(_test_directory)) {
            fs::create_directories(_test_directory);
        }

        // Start tests after a short delay
        scheduleTick<StartTestsTick>(500ms);

        co_return true;
    }

    void
    on(StartTestsTick &) {
        startTests();
    }

    void
    on(ShutdownTick &) {
        // Broadcast the Kill event
        broadcast<qb::KillEvent>();
    }

    void
    on(ReadFileResponse &response) {
        qb::io::cout() << "ClientActor received a read response for " << response.filepath.c_str() << " (ID: " << response.request_id << ")"
                       << std::endl;

        if (response.success) {
            qb::io::cout() << "File content (" << response.data->size() << " bytes): ";

            // Display the first few characters of the file
            const size_t max_display  = 50; // Limit display
            size_t       display_size = std::min(response.data->size(), max_display);

            std::string content(response.data->begin(), response.data->begin() + display_size);
            qb::io::cout() << content;

            if (response.data->size() > max_display) {
                qb::io::cout() << "... [plus " << (response.data->size() - max_display) << " bytes]";
            }
            qb::io::cout() << "\n";
        } else {
            qb::io::cout() << "Error: " << response.error_msg.c_str() << std::endl;
        }

        _pending_requests--;
        checkCompletion();
    }

    void
    on(WriteFileResponse &response) {
        qb::io::cout() << "ClientActor received a write response for " << response.filepath.c_str() << " (ID: " << response.request_id << ")"
                       << std::endl;

        if (response.success) {
            qb::io::cout() << "Write successful: " << response.bytes_written << " bytes written" << std::endl;

            // Request to read the file that was just written
            requestReadFile(response.filepath.c_str());
        } else {
            qb::io::cout() << "Write error: " << response.error_msg.c_str() << std::endl;
        }

        _pending_requests--;
        checkCompletion();
    }

    void
    on(qb::KillEvent &) {
        qb::io::cout() << "ClientActor shutting down" << std::endl;
        kill();
    }

private:
    /**
     * @brief Sleep `d`, then wake this actor with a `TickEvent`
     *
     * The safe replacement for `qb::io::async::callback([this]{ ... }, d)`, which is how this
     * example used to schedule its two delays. That overload heap-allocates a `Timeout` owned by
     * the event loop, not by the actor: nothing cancels it when the actor is killed, so it fires
     * against a destroyed object — and an `if (!is_alive()) return;` guard does not help,
     * because reading `is_alive()` IS the use-after-free.
     *
     * `spawn()` runs the body in this actor's cancellation scope and `ctx.sleep(d)` routes that
     * scope's token. The body captures only `d`, by value, and addresses the actor by id via
     * `ctx.push` — safe whether or not the actor still exists. Everything that touches actor
     * state happens in the `on(TickEvent)` handler, which only ever runs on a live actor.
     */
    template <typename TickEvent>
    void
    scheduleTick(qb::duration d) {
        spawn([d](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(d);
            ctx.template push<TickEvent>();
        });
    }

    void
    startTests() {
        qb::io::cout() << "\n=== Starting file operation tests ===\n" << std::endl;

        // Create some test files
        for (int i = 1; i <= 5; ++i) {
            std::filesystem::path filename = _test_directory / ("test_file_" + std::to_string(i) + ".txt");
            std::string           content  = "This is the content of test file " + std::to_string(i) + ".\n";
            content += "Created by ClientActor to demonstrate distributed file processing.\n";
            content += "Random line " + std::to_string(rand() % 1000) + " to make the content unique.\n";

            // Add extra content for larger files
            for (int j = 0; j < i; ++j) {
                content += "Extra line " + std::to_string(j) + " to increase file size.\n";
            }

            requestWriteFile(filename, content);
        }
    }

    void
    requestWriteFile(const std::filesystem::path &filepath, const std::string &content) {
        qb::io::cout() << "ClientActor requesting file write: " << filepath << std::endl;

        // Create a vector with the content
        auto data = std::make_shared<std::vector<char>>(content.begin(), content.end());

        // Send the request to the manager
        uint32_t request_id = _next_request_id++;
        push<WriteFileRequest>(_manager_id, filepath.string().c_str(), data, id(), request_id);

        _pending_requests++;
    }

    void
    requestReadFile(const std::filesystem::path &filepath) {
        qb::io::cout() << "ClientActor requesting file read: " << filepath << std::endl;

        // Send the request to the manager
        uint32_t request_id = _next_request_id++;
        push<ReadFileRequest>(_manager_id, filepath.string().c_str(), id(), request_id);

        _pending_requests++;
    }

    void
    checkCompletion() {
        // If all requests have been processed, wait a bit then stop
        if (_pending_requests == 0) {
            qb::io::cout() << "\n=== All tests completed ===\n" << std::endl;

            // Wait a bit then stop the system - use built-in KillEvent
            scheduleTick<ShutdownTick>(1s);
        }
    }
};

int
main(int argc, char **argv) {
    qb::io::cout() << "=== QB Core/IO Example: Distributed File Processing ===\n" << std::endl;

    // Define the test file directory
    std::filesystem::path test_dir = "./test_files";

    try {
        // Initialize the qb actor system
        qb::Main engine;

        // Create the file manager on core 0
        auto manager_id = engine.addActor<FileManager>(0);

        // Create multiple workers on different cores
        const int                num_workers = 4;
        std::vector<qb::ActorId> worker_ids;

        for (int i = 0; i < num_workers; ++i) {
            // Distribute workers across cores 1, 2, 3, ...
            int core_id = 1 + (i % 3); // Use cores 1, 2, 3

            auto worker_id = engine.addActor<FileWorker>(core_id, manager_id);
            worker_ids.push_back(worker_id);

            qb::io::cout() << "Worker " << i + 1 << " created on core " << core_id << std::endl;
        }

        // Create the client on core 0
        engine.addActor<ClientActor>(0, manager_id, test_dir);

        // Start the system
        engine.start();

        // Wait for all actors to terminate - qb::Main automatically handles signals
        engine.join();
        qb::io::cout() << "System shut down correctly" << std::endl;

    } catch (const std::exception &e) {
        qb::io::cerr() << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}