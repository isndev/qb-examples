# QB Redis Module (`qbm-redis`) Examples

This directory contains examples demonstrating various features of the `qbm-redis` module, which provides asynchronous Redis client functionality integrated with the QB C++ Actor Framework.

## Prerequisites

-   A running Redis server instance.
-   The QB Framework, including `qb-core`, `qb-io`, and `qbm-redis`, must be built.
-   Ensure the `REDIS_URI` (usually defined as `{"tcp://localhost:6379"}` or similar) at the top of each example `.cpp` file matches your Redis server configuration.

## Building the Examples

To build these examples, navigate to the root of the QB Framework and use CMake:

```bash
# From the root directory of the qb-framework
mkdir build && cd build
cmane .. -DQB_BUILD_EXAMPLES=ON # Ensure examples are generally enabled for the project
# To build a specific Redis example (e.g., example1_basic_connection)
cmake --build . --target example1_basic_connection
# Or build all qbm-redis examples (if a specific group target exists, or build all examples)
cmake --build .
```
The executables will be located in the `build/examples/qbm/redis/` directory.

## Examples Overview

---

### 1. Basic Connection & String Operations (`example1_basic_connection.cpp`)

*   **@example qbm-redis: Basic Connection and String Operations**
*   **Purpose**: Demonstrates establishing a connection to a Redis server and performing fundamental string operations like `SET`, `GET`, `INCR`, `SETEX`, and `DEL`.
*   **QB/QBM Redis Features**: `qb::io::async::init()`, `qb::redis::tcp::client`, `client.connect()`, `client.set()`, `client.get()`, `client.incr()`, `client.setex()`, `client.del()`.
*   **Run**: `./build/examples/qbm/redis/example1_basic_connection`

---

### 2. Hash Operations (`example2_hash_operations.cpp`)

*   **@example qbm-redis: Hash Data Structure Operations**
*   **Purpose**: Illustrates how to use Redis Hashes to store and retrieve structured data, such as user profiles.
*   **QB/QBM Redis Features**: `qb::redis::tcp::client`, `client.hset()`, `client.hget()`, `client.hexists()`, `client.hincrby()`, `client.hgetall()`, `client.hkeys()`, `client.hvals()`, `client.hlen()`. (Note: `hmget` is supported by `qbm-redis` but simulated with multiple `hget`s in this specific example code).
*   **Run**: `./build/examples/qbm/redis/example2_hash_operations`

---

### 3. List Operations (`example3_list_operations.cpp`)

*   **@example qbm-redis: List Data Structure Operations**
*   **Purpose**: Showcases Redis List operations for implementing FIFO queues, LIFO stacks, and other list-based functionalities.
*   **QB/QBM Redis Features**: `qb::redis::tcp::client`, `client.rpush()`, `client.lpush()`, `client.llen()`, `client.lrange()`, `client.lpop()`, `client.blpop()`, `client.lindex()`, `client.lset()`, `client.ltrim()`.
*   **Run**: `./build/examples/qbm/redis/example3_list_operations`

---

### 4. Asynchronous Operations with Actors (`example4_async_operations.cpp`)

*   **@example qbm-redis: Asynchronous Operations within QB Actors (Conceptual)**
*   **Purpose**: Illustrates how `qbm-redis` can be integrated into a QB actor system, featuring a worker actor performing Redis operations.
*   **Key Components**: `RedisWorkerActor` (performs Redis operations), `MainActor` (coordinates).
*   **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::Event`, `qb::redis::tcp::client` used within an actor. The example uses synchronous Redis calls from actor handlers; for true non-blocking behavior, `qbm-redis` asynchronous methods (e.g. `cmd_async()`, or methods with callbacks) should be used.
*   **Run**: `./build/examples/qbm/redis/example4_async_operations`

---

### 5. Publish/Subscribe Example (`example5_pubsub_example.cpp`)

*   **@example qbm-redis: Publish/Subscribe Messaging with Actors**
*   **Purpose**: Implements a basic chat-like system using Redis Pub/Sub capabilities for real-time messaging between actors.
*   **Key Components**: `PublisherActor`, `SubscriberActor` (using `qb::redis::tcp::cb_consumer`), `CoordinatorActor`.
*   **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::redis::tcp::client::publish()`, `qb::redis::tcp::cb_consumer` (for `subscribe()`, `unsubscribe()`, and message callbacks).
*   **Run**: `./build/examples/qbm/redis/example5_pubsub_example`

---

### 6. Transaction Example (`example6_transaction_example.cpp`)

*   **@example qbm-redis: Transactions and Atomic Operations with Actors**
*   **Purpose**: Demonstrates concepts of Redis transactions (`MULTI`/`EXEC`) and atomic operations (e.g., via Lua scripting) in an actor-based inventory system.
*   **Key Components**: `InventoryManagerActor`, `OrderClientActor`, `CoordinatorActor`.
*   **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::redis::tcp::client`. Shows use of various Redis commands. Highlights Lua scripting (`client.eval<T>()`) for atomicity. `qbm-redis` supports `MULTI`/`EXEC`/`WATCH` which would be idiomatic for transactional sequences.
*   **Run**: `./build/examples/qbm/redis/example6_transaction_example`

---

### 7. Stream Processor (`example7_stream_processor.cpp`)

*   **@example qbm-redis: Redis Streams with Consumer Groups and Actors**
*   **Purpose**: A scalable data processing pipeline using Redis Streams with multiple producer and consumer actors employing consumer groups.
*   **Key Components**: `SensorProducerActor` (`xadd`), `StreamConsumerActor` (`xgroup_create`, `xreadgroup`, `xack`), `CoordinatorActor`.
*   **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::ICallback`, `qb::redis::tcp::client` for Redis Stream commands (`xadd`, `xtrim`, `xlen`, `xgroup_create`, `xreadgroup`, `xack`). Multi-core deployment.
*   **Run**: `./build/examples/qbm/redis/example7_stream_processor`

---

### 8. Complex Actor System with Redis (`example8_complex_actor_system.cpp`)

*   **@example qbm-redis: Complex Actor System with Diverse Redis Usage**
*   **Purpose**: An advanced example showcasing multiple Redis patterns (work queuing via Lists, caching via Hashes/Strings, Pub/Sub, log aggregation via Streams, Lua scripting) in a complex actor system.
*   **Key Components**: `WorkerActor`, `CacheManagerActor`, `LogAggregatorActor`, `ClientActor`, `CoordinatorActor`.
*   **QB/QBM Redis Features**: Extensive use of `qb::redis::tcp::client` for Lists (`brpop`, `rpush`), Hashes (`hset`, `hget`), Strings (`setex`), Pub/Sub (`publish`), Streams (`xadd`, `xread`), and Lua (`eval`).
*   **Run**: `./build/examples/qbm/redis/example8_complex_actor_system`

---

These examples provide a practical starting point for leveraging Redis with the QB C++ Actor Framework. 