/**
 * @file examples/qbm/redis/example3_list_operations.cpp
 * @example qbm-redis: List Data Structure Operations
 *
 * @brief This example showcases operations on Redis Lists using the `qbm-redis` client.
 * It covers common use cases like FIFO queues, LIFO stacks, and various list
 * manipulation commands. It runs as a standalone `qb-io` based application.
 *
 * @details
 * The program connects to a Redis server and performs the following list operations:
 * 1.  **Basic Pushes & Length**:
 *     -   `redis.rpush(key, value...)`: Adds elements to the tail (right) of a list,
 *         demonstrating FIFO queue creation.
 *     -   `redis.lpush(key, value...)`: Adds elements to the head (left) of a list,
 *         demonstrating LIFO stack creation.
 *     -   `redis.llen(key)`: Gets the length of a list.
 * 2.  **Viewing List Content**:
 *     -   `redis.lrange(key, start, stop)`: Retrieves a range of elements from a list.
 *         Used here to display all elements (0 to -1).
 * 3.  **Popping Elements**:
 *     -   `redis.lpop(key)`: Removes and returns the first element (head/left) of a list.
 *         Used for both FIFO queue and LIFO stack processing in the example.
 * 4.  **Blocking Operations**:
 *     -   `redis.blpop(keys_vector, timeout_seconds)`: Blocking list pop. Waits for an element
 *         to become available in one of the specified lists, or times out. Returns an
 *         `std::optional<std::pair<std::string, std::string>>` (key and value).
 * 5.  **Additional List Manipulations**:
 *     -   `redis.lindex(key, index)`: Gets an element at a specific index.
 *     -   `redis.lset(key, index, value)`: Sets the value of an element at a given index.
 *     -   `redis.ltrim(key, start, stop)`: Trims a list to a specified range of elements.
 * 6.  Keys are cleaned up using `redis.del()` at the beginning and end.
 *
 * This example uses synchronous Redis client calls. For actor-based applications,
 * asynchronous equivalents with callbacks should be used.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::redis::tcp::client` and `client.connect()`.
 * - Redis List commands:
 *   - `client.rpush(key, value...)`
 *   - `client.lpush(key, value...)`
 *   - `client.llen(key)`
 *   - `client.lrange(key, start, stop)`
 *   - `client.lpop(key)` (returns `std::optional<std::string>`)
 *   - `client.blpop(std::vector<std::string> keys, int timeout)`
 *   - `client.lindex(key, index)`
 *   - `client.lset(key, index, value)`
 *   - `client.ltrim(key, start, stop)`
 * - `qb::io::async::init()` and `qb::io::cout()`.
 */

#include <redis/redis.h>
#include <qb/io/async.h>
#include <iostream>
#include <chrono>
#include <thread>

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
    std::string task_queue_key = "example:tasks:queue";
    std::string notification_stack_key = "example:notifications:stack";
    
    // Clean up any existing data
    redis.del(task_queue_key);
    redis.del(notification_stack_key);
    
    // -------- Basic LPUSH, RPUSH, LLEN operations --------
    cout << "\n===== Basic List Operations =====\n" << std::endl;
    
    // Create a FIFO queue using RPUSH (add to right) and LPOP (remove from left)
    cout << "Creating a task queue (FIFO) with RPUSH:" << std::endl;
    
    // Add tasks to the end (right) of the queue
    int rpush_count = 0;
    rpush_count += redis.rpush(task_queue_key, "Task 1: Send email");
    rpush_count += redis.rpush(task_queue_key, "Task 2: Process order");
    rpush_count += redis.rpush(task_queue_key, "Task 3: Update database");
    
    cout << "Added " << rpush_count << " tasks to the queue" << std::endl;
    cout << "Queue length: " << redis.llen(task_queue_key) << std::endl;
    
    // Create a LIFO stack using LPUSH (add to left) and LPOP (remove from left)
    cout << "\nCreating a notification stack (LIFO) with LPUSH:" << std::endl;
    
    // Add notifications to the front (left) of the stack
    int lpush_count = 0;
    lpush_count += redis.lpush(notification_stack_key, "Notification 1: New comment");
    lpush_count += redis.lpush(notification_stack_key, "Notification 2: New like");
    lpush_count += redis.lpush(notification_stack_key, "Notification 3: New follower");
    
    cout << "Added " << lpush_count << " notifications to the stack" << std::endl;
    cout << "Stack length: " << redis.llen(notification_stack_key) << std::endl;
    
    // -------- LRANGE operation to view lists without modifying them --------
    cout << "\n===== List Content Display =====\n" << std::endl;
    
    // Show all tasks in the queue (in order from oldest to newest)
    auto tasks = redis.lrange(task_queue_key, 0, -1);
    cout << "All tasks in queue (ordered by insertion time):" << std::endl;
    for (int i = 0; i < tasks.size(); i++) {
        cout << "  " << i+1 << ". " << tasks[i] << std::endl;
    }
    
    // Show all notifications in the stack (in order from newest to oldest)
    auto notifications = redis.lrange(notification_stack_key, 0, -1);
    cout << "\nAll notifications in stack (newest first):" << std::endl;
    for (int i = 0; i < notifications.size(); i++) {
        cout << "  " << i+1 << ". " << notifications[i] << std::endl;
    }
    
    // -------- LPOP and RPOP operations to process items --------
    cout << "\n===== Processing List Items =====\n" << std::endl;
    
    // Process tasks from the FIFO queue (LPOP takes from front/left)
    cout << "Processing tasks from queue (FIFO with LPOP):" << std::endl;
    auto task1 = redis.lpop(task_queue_key);
    auto task2 = redis.lpop(task_queue_key);
    
    if (task1.has_value()) {
        cout << "  Processed: " << *task1 << std::endl;
    }
    
    if (task2.has_value()) {
        cout << "  Processed: " << *task2 << std::endl;
    }
    
    cout << "Tasks remaining: " << redis.llen(task_queue_key) << std::endl;
    
    // Process notifications from the LIFO stack (LPOP takes from front/left)
    cout << "\nProcessing notifications from stack (LIFO with LPOP):" << std::endl;
    auto notif1 = redis.lpop(notification_stack_key);
    
    if (notif1.has_value()) {
        cout << "  Processed: " << *notif1 << std::endl;
    }
    
    cout << "Notifications remaining: " << redis.llen(notification_stack_key) << std::endl;
    
    // -------- Blocking operations --------
    cout << "\n===== Blocking Operations =====\n" << std::endl;
    
    // Demonstrate BLPOP with timeout
    cout << "Demonstrating BLPOP with 2 second timeout..." << std::endl;
    
    // Create a new list with a single item
    std::string temp_key = "example:temp:list";
    redis.del(temp_key);
    redis.rpush(temp_key, "Last item");
    
    // Pop the only item with BLPOP
    auto blpop_result = redis.blpop({temp_key}, 2);
    
    if (blpop_result.has_value()) {
        cout << "  BLPOP result - Key: " << blpop_result->first
             << ", Value: " << blpop_result->second << std::endl;
    }
    
    // Try BLPOP again with timeout (should time out as list is now empty)
    cout << "Waiting 2 seconds for BLPOP to timeout on empty list..." << std::endl;
    blpop_result = redis.blpop({temp_key}, 2);
    
    if (!blpop_result.has_value()) {
        cout << "  BLPOP timed out as expected" << std::endl;
    }
    
    // -------- Additional list operations --------
    cout << "\n===== Additional List Operations =====\n" << std::endl;
    
    // Insert an item at a specific position
    redis.lset(task_queue_key, 0, "Updated: " + redis.lindex(task_queue_key, 0).value_or("New task"));
    cout << "Updated task using LSET" << std::endl;
    
    // Add new tasks and trim the list to keep only 3 most recent
    redis.rpush(task_queue_key, "Task 4: Send notifications");
    redis.rpush(task_queue_key, "Task 5: Generate report");
    cout << "Added 2 more tasks, queue now has " << redis.llen(task_queue_key) << " tasks" << std::endl;
    
    // Trim the list to keep only 3 most recent tasks
    redis.ltrim(task_queue_key, -3, -1);
    cout << "Trimmed queue to keep only 3 most recent tasks" << std::endl;
    
    // Show the final task list
    auto final_tasks = redis.lrange(task_queue_key, 0, -1);
    cout << "Final tasks in queue:" << std::endl;
    for (int i = 0; i < final_tasks.size(); i++) {
        cout << "  " << i+1 << ". " << final_tasks[i] << std::endl;
    }
    
    // Clean up
    cout << "\nCleaning up..." << std::endl;
    redis.del(task_queue_key, notification_stack_key, temp_key);
    cout << "List operations completed successfully!" << std::endl;
    
    return 0;
} 