# QB Redis Module (`qbm-redis`) Examples

This directory contains examples demonstrating various features of the `qbm-redis` module, which provides asynchronous
Redis client functionality integrated with the QB C++ Actor Framework.

## Prerequisites

- A running Redis server instance.
- The QB Framework, including `qb-core`, `qb-io`, and `qbm-redis`, must be built.
- Ensure the `REDIS_URI` (usually defined as `{"tcp://localhost:6379"}` or similar) at the top of each example `.cpp`
  file matches your Redis server configuration.

## Building the Examples

To build these examples, navigate to the root of the QB Framework and use CMake:

```bash
# From the root directory of the qb-framework
mkdir build && cd build
cmake .. -DQB_BUILD_EXAMPLES=ON # Ensure examples are generally enabled for the project
# To build a specific Redis example (e.g., example1_basic_connection)
cmake --build . --target example1_basic_connection
# Or build all qbm-redis examples (if a specific group target exists, or build all examples)
cmake --build .
```

The executables will be located in the `build/examples/qbm/redis/` directory.

## Examples Overview

---

### 1. Basic Connection & String Operations (`example1_basic_connection.cpp`)

* **@example qbm-redis: Basic Connection and String Operations**
* **Purpose**: Demonstrates establishing a connection to a Redis server and performing fundamental string operations
  like `SET`, `GET`, `INCR`, `SETEX`, and `DEL`.
* **QB/QBM Redis Features**: `qb::io::async::init()`, `qb::redis::tcp::client`, `client.connect()`, `client.set()`,
  `client.get()`, `client.incr()`, `client.setex()`, `client.del()`.
* **Run**: `./build/examples/qbm/redis/example1_basic_connection`

---

### 2. Hash Operations (`example2_hash_operations.cpp`)

* **@example qbm-redis: Hash Data Structure Operations**
* **Purpose**: Illustrates how to use Redis Hashes to store and retrieve structured data, such as user profiles.
* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `client.hset()`, `client.hget()`, `client.hexists()`,
  `client.hincrby()`, `client.hgetall()`, `client.hkeys()`, `client.hvals()`, `client.hlen()`. (Note: `hmget` is
  supported by `qbm-redis` but simulated with multiple `hget`s in this specific example code).
* **Run**: `./build/examples/qbm/redis/example2_hash_operations`

---

### 3. List Operations (`example3_list_operations.cpp`)

* **@example qbm-redis: List Data Structure Operations**
* **Purpose**: Showcases Redis List operations for implementing FIFO queues, LIFO stacks, and other list-based
  functionalities.
* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `client.rpush()`, `client.lpush()`, `client.llen()`,
  `client.lrange()`, `client.lpop()`, `client.blpop()`, `client.lindex()`, `client.lset()`, `client.ltrim()`.
* **Run**: `./build/examples/qbm/redis/example3_list_operations`

---

### 4. Asynchronous Operations with Actors (`example4_async_operations.cpp`)

* **@example qbm-redis: Asynchronous Operations within QB Actors (Coroutine API)**
* **Purpose**: Illustrates how `qbm-redis` is integrated into a QB actor system, featuring a worker actor performing
  Redis operations with the coroutine API.
* **Key Components**: `RedisWorkerActor` (performs Redis operations), `MainActor` (coordinates).
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::Event`, and `qb::redis::tcp::client` inside an actor.
  Nothing here is a blocking call: `onInit()` is a `qb::io::async::task<bool>` that does `co_await _redis.connect()`,
  and each `RedisDataEvent` spawns a coroutine that does `co_await _redis.set(...)`, `co_await _redis.incr(...)`,
  `co_await _redis.get(...)`, reading `Reply<T>::ok()` / `result()` between steps
  (`example4_async_operations.cpp:2-3`, `:11-19`, `:29-33`).
* **Run**: `./build/examples/qbm/redis/example4_async_operations`

---

### 5. Publish/Subscribe Example (`example5_pubsub_example.cpp`)

* **@example qbm-redis: Publish/Subscribe Messaging with Actors**
* **Purpose**: Implements a basic chat-like system using Redis Pub/Sub capabilities for real-time messaging between
  actors.
* **Key Components**: `PublisherActor`, `SubscriberActor` (using `qb::redis::tcp::co_consumer`), `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `co_await client.publish(channel, message)`
  (`example5_pubsub_example.cpp:149`), and **`qb::redis::tcp::co_consumer`** — the *coroutine* Pub/Sub consumer
  (`:177`) — with `co_await consumer.subscribe(channel)` → `Reply<qb::redis::subscription>` (`:228`) and a
  `while (auto msg = co_await _consumer.receive()) { ... }` loop (`:207`) instead of a message callback.
  Both consumers are real types (`qbm/redis/src/qbm/redis/redis.h:1705-1706`): `cb_consumer` is the
  callback-driven one, `co_consumer` the coroutine one. This example uses `co_consumer`.
* **Run**: `./build/examples/qbm/redis/example5_pubsub_example`

---

### 6. Transaction Example (`example6_transaction_example.cpp`)

* **@example qbm-redis: Transactions and Atomic Operations with Actors (Coroutine API)**
* **Purpose**: An inventory/ordering simulation. Note what it does **not** contain: there is no `MULTI`, no `EXEC`, no
  `WATCH` and no Lua `eval` anywhere in this file. "Atomic" here means Redis's own single-command atomicity — the
  stock decrement is a `HINCRBY`, which needs no transaction. (For real Lua scripting see example 8, which uses
  `co_await _redis.eval<long long>(script, {}, {...})` at `example8_complex_actor_system.cpp:363` and `:417`.)
* **Key Components**: `InventoryManagerActor`, `OrderClientActor` (several instances), `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::io::async::task<bool> onInit()` with
  `co_await _redis.connect()`, and coroutines spawned from synchronous event handlers. Commands used
  (`example6_transaction_example.cpp:30-40`): hashes `hset` / `hget` / `hgetall` / `hincrby`; keys `keys` / `del`;
  strings `set` / `get` / `incr` / `setex`; lists `lpush` / `lrange`. Reads results through `Reply<T>::ok()` and
  `result()`; `addRefActor` returns a `qb::ActorHandle<T>` with `.valid()` / `.id()`.
* **Run**: `./build/examples/qbm/redis/example6_transaction_example`

---

### 7. Stream Processor (`example7_stream_processor.cpp`)

* **@example qbm-redis: Redis Streams with Consumer Groups and Actors**
* **Purpose**: A scalable data processing pipeline using Redis Streams with multiple producer and consumer actors
  employing consumer groups.
* **Key Components**: `SensorProducerActor` (`xadd`), `StreamConsumerActor` (`xgroup_create`, `xreadgroup`, `xack`),
  `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::ICallback`, `qb::redis::tcp::client` for Redis Stream
  commands (`xadd`, `xtrim`, `xlen`, `xgroup_create`, `xreadgroup`, `xack`). Multi-core deployment.
* **Run**: `./build/examples/qbm/redis/example7_stream_processor`

---

### 8. Complex Actor System with Redis (`example8_complex_actor_system.cpp`)

* **@example qbm-redis: Complex Actor System with Diverse Redis Usage**
* **Purpose**: An advanced example showcasing multiple Redis patterns (work queuing via Lists, caching via
  Hashes/Strings, Pub/Sub, log aggregation via Streams, Lua scripting) in a complex actor system.
* **Key Components**: `WorkerActor`, `CacheManagerActor`, `LogAggregatorActor`, `ClientActor`, `CoordinatorActor`.
* **QB/QBM Redis Features**: Extensive use of `qb::redis::tcp::client` for Lists (`brpop`, `rpush`), Hashes (`hset`,
  `hget`), Strings (`setex`), Pub/Sub (`publish`), Streams (`xadd`, `xread`), and Lua (`eval`).
* **Run**: `./build/examples/qbm/redis/example8_complex_actor_system`

---

### 9. Coroutine API without Actors (`coro_example.cpp`)

* **@example qbm-redis: Coroutine API on pure qb-io — NO ACTORS**
* **Purpose**: The smallest complete picture of the coroutine API. Commands are issued directly on
  `qb::redis::tcp::client` and `co_await`ed; there is no wrapper class, no callback, and no actor system — the driver
  is `qb::io::async::init()` + `coro_scheduler().spawn(...)` + `run_until(running)` (`coro_example.cpp:196-208`).
* **Key Components**: four free coroutines called in sequence from `run_all_examples()` (`:175-193`):
    1. `example_simple_get_set()` — `co_await redis.connect()`, `set`, `get`; `get()` yields
       `Reply<std::optional<std::string>>` (`:28-54`).
    2. `example_sequential_operations()` — a hash written field by field, then read back (`:60-88`).
    3. `example_multiple_keys()` — three `GET`s issued **in parallel** with
       `co_await qb::io::async::when_all(...)` (`:124`). `when_all` takes real `task<T>` objects, not raw command
       awaiters, so each `GET` is wrapped in a one-line local coroutine; because they share one client they are
       pipelined into a single round-trip group rather than three (`:94-139`).
    4. `example_error_handling()` — the miss case: `ok()` true, `result()` empty, for a key that does not exist
       (`:145-166`).
* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `co_await client.<command>()`, `Reply<T>::ok()` / `result()`,
  `qb::io::async::when_all`, and the standalone `run_until` scaffolding.
* **Run**: `./build/examples/qbm/redis/coro_example`

---

These examples provide a practical starting point for leveraging Redis with the QB C++ Actor Framework. 