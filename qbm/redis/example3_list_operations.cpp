/**
 * @file examples/qbm/redis/example3_list_operations.cpp
 * @example qbm-redis: List Data Structure Operations
 *
 * @brief Showcases operations on Redis Lists using the `qbm-redis` client, using the modern
 * **standalone qb-io** coroutine flow (pure qb-io — no actor framework). Covers FIFO queues,
 * LIFO stacks, and various list manipulation commands.
 *
 * @details
 * The qbm-redis client is coroutine-first: every command returns a `redis_awaiter` that
 * you `co_await` to obtain a `qb::redis::Reply<T>`. All Redis work lives in a free
 * `qb::io::async::task<void>` coroutine; `qb::io::async::run_until(running)` drives the
 * event loop until the coroutine signals completion (it flips `running` to false on any
 * exit via a scope guard).
 *
 * The program connects to a Redis server and performs the following list operations:
 * 1.  **Basic Pushes & Length**:
 *     -   `co_await redis.rpush(key, value...)` -> `Reply<long long>` (list length after push)
 *     -   `co_await redis.lpush(key, value...)` -> `Reply<long long>` (list length after push)
 *     -   `co_await redis.llen(key)`            -> `Reply<long long>` (current list length)
 * 2.  **Viewing List Content**:
 *     -   `co_await redis.lrange(key, start, stop)` -> `Reply<vector<string>>` (range of elements)
 * 3.  **Popping Elements**:
 *     -   `co_await redis.lpop(key)` -> `Reply<optional<string>>` (head element or nil)
 * 4.  **Blocking Operations**:
 *     -   `co_await redis.blpop(keys_vector, timeout_seconds)` ->
 *         `Reply<optional<pair<string,string>>>` (key + value, or nil on timeout)
 * 5.  **Additional List Manipulations**:
 *     -   `co_await redis.lindex(key, index)` -> `Reply<optional<string>>`
 *     -   `co_await redis.lset(key, index, value)` -> `Reply<status>`
 *     -   `co_await redis.ltrim(key, start, stop)`  -> `Reply<status>`
 * 6.  Keys are cleaned up with `co_await redis.del()` at the beginning and end.
 *
 * QB/QBM Redis Features Demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `qb::redis::tcp::client` + `co_await client.connect()`.
 * - `co_await client.<command>()` and `qb::redis::Reply<T>`: `ok()` and `result()`.
 * - `qb::io::async::when_all(...)` — read independent lists in parallel / pipelined.
 * - `qb::io::cout()` — thread-safe console output.
 */

#include <iostream>

#include <redis/redis.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>

// Redis Configuration - must be in initializer list format
#define REDIS_URI {"tcp://localhost:6379"}

// All Redis work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_list_operations(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::redis::tcp::client redis{REDIS_URI};

    // Connect - co_await yields a bool (true on success).
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis" << std::endl;
        co_return;
    }
    qb::io::cout() << "Connected to Redis successfully!" << std::endl;

    // Generate unique keys for this example to avoid collisions
    std::string task_queue_key         = "example:tasks:queue";
    std::string notification_stack_key = "example:notifications:stack";

    // Clean up any existing data
    (void) (co_await redis.del(task_queue_key));
    (void) (co_await redis.del(notification_stack_key));

    // -------- Basic LPUSH, RPUSH, LLEN operations --------
    qb::io::cout() << "\n===== Basic List Operations =====\n" << std::endl;

    // Create a FIFO queue using RPUSH (add to right) and LPOP (remove from left).
    // rpush returns Reply<long long> = list length after push.
    qb::io::cout() << "Creating a task queue (FIFO) with RPUSH:" << std::endl;

    (void) (co_await redis.rpush(task_queue_key, "Task 1: Send email"));
    (void) (co_await redis.rpush(task_queue_key, "Task 2: Process order"));
    (void) (co_await redis.rpush(task_queue_key, "Task 3: Update database"));

    qb::io::cout() << "Added 3 tasks to the queue" << std::endl;

    // llen returns Reply<long long>
    auto q_len = co_await redis.llen(task_queue_key);
    qb::io::cout() << "Queue length: " << q_len.result() << std::endl;

    // Create a LIFO stack using LPUSH (add to left) and LPOP (remove from left).
    qb::io::cout() << "\nCreating a notification stack (LIFO) with LPUSH:" << std::endl;

    (void) (co_await redis.lpush(notification_stack_key, "Notification 1: New comment"));
    (void) (co_await redis.lpush(notification_stack_key, "Notification 2: New like"));
    (void) (co_await redis.lpush(notification_stack_key, "Notification 3: New follower"));

    qb::io::cout() << "Added 3 notifications to the stack" << std::endl;

    auto s_len = co_await redis.llen(notification_stack_key);
    qb::io::cout() << "Stack length: " << s_len.result() << std::endl;

    // -------- LRANGE operation to view lists without modifying them --------
    qb::io::cout() << "\n===== List Content Display =====\n" << std::endl;

    // Both LRANGEs read different keys and don't depend on each other, so fetch
    // them in PARALLEL with when_all instead of one-by-one. when_all takes real
    // qb::io::async::task<T> objects, not raw command awaiters, so we wrap each
    // LRANGE in a tiny local coroutine. The `range_all` lambda outlives the
    // when_all call, so capturing `&redis` by reference is safe. Because both
    // commands run on the same client, the reads are pipelined into a single
    // round-trip group. lrange yields Reply<vector<string>>.
    auto range_all = [&redis](std::string key) -> qb::io::async::task<qb::redis::Reply<std::vector<std::string>>> {
        co_return co_await redis.lrange(key, 0, -1);
    };

    auto [tasks_r, notifs_r] = co_await qb::io::async::when_all(range_all(task_queue_key), range_all(notification_stack_key));

    qb::io::cout() << "All tasks in queue (ordered by insertion time):" << std::endl;
    if (tasks_r.ok()) {
        const auto &tasks = tasks_r.result();
        for (size_t i = 0; i < tasks.size(); i++) {
            qb::io::cout() << "  " << (i + 1) << ". " << tasks[i] << std::endl;
        }
    }

    qb::io::cout() << "\nAll notifications in stack (newest first):" << std::endl;
    if (notifs_r.ok()) {
        const auto &notifications = notifs_r.result();
        for (size_t i = 0; i < notifications.size(); i++) {
            qb::io::cout() << "  " << (i + 1) << ". " << notifications[i] << std::endl;
        }
    }

    // -------- LPOP and RPOP operations to process items --------
    qb::io::cout() << "\n===== Processing List Items =====\n" << std::endl;

    // lpop returns Reply<optional<string>>; use result().has_value() / *result()
    qb::io::cout() << "Processing tasks from queue (FIFO with LPOP):" << std::endl;
    auto task1_r = co_await redis.lpop(task_queue_key);
    auto task2_r = co_await redis.lpop(task_queue_key);

    if (task1_r.ok() && task1_r.result().has_value()) {
        qb::io::cout() << "  Processed: " << *task1_r.result() << std::endl;
    }
    if (task2_r.ok() && task2_r.result().has_value()) {
        qb::io::cout() << "  Processed: " << *task2_r.result() << std::endl;
    }

    auto remaining_r = co_await redis.llen(task_queue_key);
    qb::io::cout() << "Tasks remaining: " << remaining_r.result() << std::endl;

    qb::io::cout() << "\nProcessing notifications from stack (LIFO with LPOP):" << std::endl;
    auto notif1_r = co_await redis.lpop(notification_stack_key);
    if (notif1_r.ok() && notif1_r.result().has_value()) {
        qb::io::cout() << "  Processed: " << *notif1_r.result() << std::endl;
    }

    auto notif_rem_r = co_await redis.llen(notification_stack_key);
    qb::io::cout() << "Notifications remaining: " << notif_rem_r.result() << std::endl;

    // -------- Blocking operations --------
    qb::io::cout() << "\n===== Blocking Operations =====\n" << std::endl;

    // Demonstrate BLPOP with timeout.
    // blpop returns Reply<optional<pair<string, string>>> (list-key + value, or nil on timeout).
    qb::io::cout() << "Demonstrating BLPOP with 2 second timeout..." << std::endl;

    // Create a new list with a single item
    std::string temp_key = "example:temp:list";
    (void) (co_await redis.del(temp_key));
    (void) (co_await redis.rpush(temp_key, "Last item"));

    // Pop the only item with BLPOP
    auto blpop1_r = co_await redis.blpop({temp_key}, 2);
    if (blpop1_r.ok() && blpop1_r.result().has_value()) {
        qb::io::cout() << "  BLPOP result - Key: " << blpop1_r.result()->first << ", Value: " << blpop1_r.result()->second << std::endl;
    }

    // Try BLPOP again with timeout (should time out as the list is now empty)
    qb::io::cout() << "Waiting 2 seconds for BLPOP to timeout on empty list..." << std::endl;
    auto blpop2_r = co_await redis.blpop({temp_key}, 2);
    if (!blpop2_r.result().has_value()) {
        qb::io::cout() << "  BLPOP timed out as expected" << std::endl;
    }

    // -------- Additional list operations --------
    qb::io::cout() << "\n===== Additional List Operations =====\n" << std::endl;

    // lindex returns Reply<optional<string>>
    auto        idx0_r       = co_await redis.lindex(task_queue_key, 0);
    std::string current_task = (idx0_r.ok() && idx0_r.result().has_value()) ? *idx0_r.result() : "New task";

    // lset returns Reply<status>; ok() indicates success
    (void) (co_await redis.lset(task_queue_key, 0, "Updated: " + current_task));
    qb::io::cout() << "Updated task using LSET" << std::endl;

    // Add new tasks and trim the list to keep only 3 most recent
    (void) (co_await redis.rpush(task_queue_key, "Task 4: Send notifications"));
    (void) (co_await redis.rpush(task_queue_key, "Task 5: Generate report"));

    auto after_push_r = co_await redis.llen(task_queue_key);
    qb::io::cout() << "Added 2 more tasks, queue now has " << after_push_r.result() << " tasks" << std::endl;

    // ltrim returns Reply<status>
    (void) (co_await redis.ltrim(task_queue_key, -3, -1));
    qb::io::cout() << "Trimmed queue to keep only 3 most recent tasks" << std::endl;

    // Show the final task list
    auto final_r = co_await redis.lrange(task_queue_key, 0, -1);
    qb::io::cout() << "Final tasks in queue:" << std::endl;
    if (final_r.ok()) {
        const auto &final_tasks = final_r.result();
        for (size_t i = 0; i < final_tasks.size(); i++) {
            qb::io::cout() << "  " << (i + 1) << ". " << final_tasks[i] << std::endl;
        }
    }

    // Clean up
    qb::io::cout() << "\nCleaning up..." << std::endl;
    (void) (co_await redis.del(task_queue_key, notification_stack_key, temp_key));
    qb::io::cout() << "List operations completed successfully!" << std::endl;

    co_return;
}

int
main() {
    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_list_operations(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    return 0;
}
