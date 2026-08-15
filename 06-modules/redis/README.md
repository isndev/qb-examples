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
# To build a specific Redis example (e.g., qb-example-modules-redis-connect)
cmake --build . --target qb-example-modules-redis-connect
# Or build all qbm-redis examples (if a specific group target exists, or build all examples)
cmake --build .
```

The executables will be located in the `build/examples/06-modules/redis/` directory — except
`example2_hash_operations`, `example3_list_operations` and `example8_complex_actor_system`, which
have not moved: they are in `build/examples/qbm/redis/`, the pre-3.0 holding directory. Their
replacements have LANDED (`02-data-types` merges the first two and adds sets; `07-scripting` and
`10-cache-actor` between them cover the third), so those three are retirable — the retirement is
its own step and has not been taken.

**Every program added since 3.0 deletes the keys it wrote**, on the failure path as well as the
success one. That is not tidiness: `06-streams` writes ~1,000,000 stream entries per run and leaves
them (measured: `XLEN` 1,000,546 and ~240 MB after one run from a clean key), and a program whose
cost depends on what a previous run left behind cannot be judged by a timeout.

## Examples Overview

---

### 1. Basic Connection & String Operations (`01-connect.cpp`)

* **@example qbm-redis: Basic Connection and String Operations**
* **Purpose**: Demonstrates establishing a connection to a Redis server and performing fundamental string operations
  like `SET`, `GET`, `INCR`, `SETEX`, and `DEL`.
* **QB/QBM Redis Features**: `qb::io::async::init()`, `qb::redis::tcp::client`, `client.connect()`, `client.set()`,
  `client.get()`, `client.incr()`, `client.setex()`, `client.del()`.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-connect`

---

### 2. Data Types (`02-data-types.cpp`) — **the merge, plus sets**

* **Purpose**: string, hash, list and **set** in one program, and the question the older pair never
  answered: which one to reach for. The organising idea is that the C++ TYPE of each command's
  `Reply` states the semantics — `get` → `optional<string>` (may be ABSENT), `hgetall` →
  `unordered_map` (an object, whole), `lrange` → `vector` (order is part of the value), `smembers` →
  `unordered_set` (no order, no duplicates), `sismember` → `bool` (the question, not the set).
* **QB/QBM Redis Features**: `set`/`get`/`append`/`strlen`/`incrby`/`mset`/`mget`;
  `hset`/`hget`/`hgetall`/`hincrby`/`hexists`/`hdel`/`hlen`; `rpush`/`lpush`/`lrange`/`llen`/`ltrim`/
  `lpop`/`rpop`; `sadd`/`smembers`/`sismember`/`scard`/`sinter`/`sunion`/`sdiff`/`srem`. Sets had no
  demonstrator anywhere in the corpus before this file.
* **One measured gotcha**: `qb::unordered_set` is the vendored ska flat hash set and predates C++20's
  `contains` — use `count(k) == 1`.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-data-types`

---

### 2b. Hash Operations (`example2_hash_operations.cpp`) — *superseded, awaiting retirement*

* **@example qbm-redis: Hash Data Structure Operations**
* **Purpose**: Illustrates how to use Redis Hashes to store and retrieve structured data, such as user profiles.
* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `client.hset()`, `client.hget()`, `client.hexists()`,
  `client.hincrby()`, `client.hgetall()`, `client.hkeys()`, `client.hvals()`, `client.hlen()`. (Note: `hmget` is
  supported by `qbm-redis` but simulated with multiple `hget`s in this specific example code).
* **Run**: `./build/examples/qbm/redis/example2_hash_operations`

---

### 3. List Operations (`example3_list_operations.cpp`) — *superseded, awaiting retirement*

* **@example qbm-redis: List Data Structure Operations**
* **Purpose**: Showcases Redis List operations for implementing FIFO queues, LIFO stacks, and other list-based
  functionalities.
* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `client.rpush()`, `client.lpush()`, `client.llen()`,
  `client.lrange()`, `client.lpop()`, `client.blpop()`, `client.lindex()`, `client.lset()`, `client.ltrim()`.
* **Run**: `./build/examples/qbm/redis/example3_list_operations`

---

### 4. Asynchronous Operations with Actors (`10-cache-actor.cpp`)

* **@example qbm-redis: Asynchronous Operations within QB Actors (Coroutine API)**
* **Purpose**: Illustrates how `qbm-redis` is integrated into a QB actor system, featuring a worker actor performing
  Redis operations with the coroutine API.
* **Key Components**: `RedisWorkerActor` (performs Redis operations), `MainActor` (coordinates).
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::Event`, and `qb::redis::tcp::client` inside an actor.
  Nothing here is a blocking call: `onInit()` is a `qb::io::async::task<bool>` that does `co_await _redis.connect()`,
  and each `RedisDataEvent` spawns a coroutine that does `co_await _redis.set(...)`, `co_await _redis.incr(...)`,
  `co_await _redis.get(...)`, reading `Reply<T>::ok()` / `result()` between steps
  (`10-cache-actor.cpp:10-11`, `:19-27`, `:37-41`).
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-cache-actor`

---

### 5. Publish/Subscribe Example (`04-pubsub.cpp`)

* **@example qbm-redis: Publish/Subscribe Messaging with Actors**
* **Purpose**: Implements a basic chat-like system using Redis Pub/Sub capabilities for real-time messaging between
  actors.
* **Key Components**: `PublisherActor`, `SubscriberActor` (using `qb::redis::tcp::co_consumer`), `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `co_await client.publish(channel, message)`
  (`04-pubsub.cpp:173`), and **`qb::redis::tcp::co_consumer`** — the *coroutine* Pub/Sub consumer
  (`:199`) — with `co_await consumer.subscribe(channel)` → `Reply<qb::redis::subscription>` (`:273`) and a
  `while (auto msg = co_await _consumer.receive()) { ... }` loop (`:250`) instead of a message callback. Read that
  loop's capture list: it takes `this` only for the consumer it awaits, and copies everything it *reads*
  (`name = _name`, `coordinator = _coordinator_id`) before the first `co_await`. The loop resumes when
  `~RedisCoroConsumer` closes the message channel — i.e. while the actor is being destroyed — so a member read after
  the resume is a use-after-free, and was one.
  Both consumers are real types (`qbm/redis/src/qbm/redis/redis.h:1705-1706`): `cb_consumer` is the
  callback-driven one, `co_consumer` the coroutine one. This example uses `co_consumer`.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-pubsub`

---

### 6. Transactions (`05-transactions.cpp`) — **rewritten**

Titled *Transactions and Atomic Operations*, 716 lines, and — measured — calling no `MULTI`,
`EXEC`, `WATCH` or `DISCARD`. It was the one file in the restructured corpus whose filename was not
true of it. It now exercises all five verbs against a live Redis, with a gated verdict for each,
plus the trap that decides whether a MULTI block works: a queued command answers `+QUEUED`, and a
TYPED client reacts three different ways to that — `status` gives `ok()==true` with the string
`QUEUED`, `long long` gives `ok()==false` for a perfectly queued command, and
`std::optional<std::string>` gives `ok()==true` with the value literally set to `"QUEUED"`, which
is the silent one. It also shows how to tell an aborted `EXEC` (RESP nil, `raw()->is_null()`) from
a genuine parse error, and how to read a heterogeneous batch through `raw()`.

#### The original notes


* **@example qbm-redis: Transactions and Atomic Operations with Actors (Coroutine API)**
* **Purpose**: An inventory/ordering simulation. Note what it does **not** contain: there is no `MULTI`, no `EXEC`, no
  `WATCH` and no Lua `eval` anywhere in this file. "Atomic" here means Redis's own single-command atomicity — the
  stock decrement is a `HINCRBY`, which needs no transaction. (For real Lua scripting see example 8, which uses
  `co_await _redis.eval<long long>(script, {}, {...})` at `example8_complex_actor_system.cpp:363` and `:417`.)
* **Key Components**: `InventoryManagerActor`, `OrderClientActor` (several instances), `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::io::async::task<bool> onInit()` with
  `co_await _redis.connect()`, and coroutines spawned from synchronous event handlers. Commands used
  (`05-transactions.cpp:39-49`): hashes `hset` / `hget` / `hgetall` / `hincrby`; keys `keys` / `del`;
  strings `set` / `get` / `incr` / `setex`; lists `lpush` / `lrange`. Reads results through `Reply<T>::ok()` and
  `result()`; `addRefActor` returns a `qb::ActorHandle<T>` with `.valid()` / `.id()`.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-transactions`

---

### 7. Stream Processor (`06-streams.cpp`)

* **@example qbm-redis: Redis Streams with Consumer Groups and Actors**
* **Purpose**: A scalable data processing pipeline using Redis Streams with multiple producer and consumer actors
  employing consumer groups.
* **Key Components**: `SensorProducerActor` (`xadd`), `StreamConsumerActor` (`xgroup_create`, `xreadgroup`, `xack`),
  `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::ICallback`, `qb::redis::tcp::client` for Redis Stream
  commands (`xadd`, `xtrim`, `xlen`, `xgroup_create`, `xreadgroup`, `xack`). Multi-core deployment.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-streams`

---

### 7b. Scripting (`07-scripting.cpp`) — **new**

* **Purpose**: running your logic INSIDE Redis, in the three forms the server offers, and the trap
  that decides whether an EVALSHA deployment survives a restart. It is the direct sequel to
  `05-transactions`: a MULTI block cannot READ a value, so "write only if the version is still the
  one I read" is not expressible as a transaction — a script has no such limit.
* **QB/QBM Redis Features**: `eval<T>`, `evalRo<T>`, `script_load`, `script_exists`, `evalsha<T>`,
  `function_load`, `function_list`, `function_delete`, `fcall<T>`, `fcallRo<T>`.
* **The trap**: the script cache lives in the server's MEMORY. An unknown SHA is `NOSCRIPT`, which is
  what every cached SHA becomes after a restart or a failover, so production code is "EVALSHA, and on
  NOSCRIPT send the body once with EVAL and retry". Redis 7 Functions remove the case entirely — a
  library is loaded BY NAME, persisted and replicated. Needs Redis 7.0+ and says so out loud.
* **Deliberately absent**: `SCRIPT FLUSH` and `FUNCTION FLUSH`. Both are server-wide and would delete
  every other client's work on a shared Redis; the NOSCRIPT case is provoked with a well-formed SHA
  that was never loaded.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-scripting`

---

### 7c. Sorted Sets and Expiry (`08-sorted-sets-and-ttl.cpp`) — **new**

* **Purpose**: the structure that keeps the ORDER for you (a leaderboard: "top 3" is a range read,
  not a sort; "what rank am I" is a lookup), the same structure scored by TIME (a sliding-window rate
  limiter in three commands and no timer), the expiry rules, and the cursor SCAN you must use instead
  of `KEYS`.
* **QB/QBM Redis Features**: `zadd`/`zincrby`/`zcard`/`zscore`/`zrevrange`/`zrevrank`/`zrangebyscore`/
  `zremrangebyscore`/`zrem`, `qb::redis::score_member`, the interval types and `LimitOptions`;
  `expire`/`ttl`/`persist`/`setex`; `scan` + `qb::redis::scan<>`.
* **Two measured gotchas**: `LeftBoundedInterval<double>` accepts only `OPEN` and `RIGHT_OPEN` and
  THROWS `qb::redis::Error` on `CLOSED` (`redis.cpp:127-141`) — for `[300, +inf)` the open side is the
  right one. And a plain `SET` CLEARS a key's TTL while `INCR` keeps it; `-1` means "no expiry" and
  `-2` means "no key", which are two different answers.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-sorted-sets-and-ttl`

---

### 7d. Reliability (`09-reliability.cpp`) — **new**

* **Purpose**: what every other Redis example assumes away. Bounded connect retry, auto-reconnect,
  what happens to a command that was IN FLIGHT when the link died, blocking commands that park a
  coroutine without blocking the loop, `INFO` as a health probe, and the TLS seam.
* **QB/QBM Redis Features**: `qb::redis::RetryPolicy` and its builders, `connect_with_retry`,
  `enable_auto_reconnect`/`disable_auto_reconnect`, `is_connected`, `client_id`/`client_kill`,
  `brpop`, `info`, `qb::redis::tcp::ssl::client`.
* **Four things it measures rather than asserts**: `RetryPolicy{}` defaults to UNLIMITED attempts (a
  startup hang waiting to happen); a dropped connection FAILS every pending reply rather than
  stranding it, so `ok()` is false and `raw()` is `nullptr` — that null is the discriminator between
  a dead link and a nil value; auto-reconnect restores the CONNECTION, never the request; and a
  1 s `BRPOP` parks the coroutine while a second coroutine on the same thread keeps ticking.
* **It also documents a silence**: `coro_scheduler().spawn(t)` never observes the task's result, so an
  exception escaping a spawned task is discarded without a word. The `guarded()` wrapper at the
  bottom of the file is the five lines that end it.
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-reliability`

---

### 8. Complex Actor System with Redis (`example8_complex_actor_system.cpp`) — *superseded, awaiting retirement*

* **@example qbm-redis: Complex Actor System with Diverse Redis Usage**
* **Purpose**: An advanced example showcasing multiple Redis patterns (work queuing via Lists, caching via
  Hashes/Strings, Pub/Sub, log aggregation via Streams, Lua scripting) in a complex actor system.
* **Key Components**: `WorkerActor`, `CacheManagerActor`, `LogAggregatorActor`, `ClientActor`, `CoordinatorActor`.
* **QB/QBM Redis Features**: Extensive use of `qb::redis::tcp::client` for Lists (`brpop`, `rpush`), Hashes (`hset`,
  `hget`), Strings (`setex`), Pub/Sub (`publish`), Streams (`xadd`, `xread`), and Lua (`eval`).
* **Run**: `./build/examples/qbm/redis/example8_complex_actor_system`

---

### 9. Coroutine API without Actors (`03-coroutines-and-pipelining.cpp`)

* **@example qbm-redis: Coroutine API on pure qb-io — NO ACTORS**
* **Purpose**: The smallest complete picture of the coroutine API. Commands are issued directly on
  `qb::redis::tcp::client` and `co_await`ed; there is no wrapper class, no callback, and no actor system — the driver
  is `qb::io::async::init()` + `coro_scheduler().spawn(...)` + `run_until(running)` (`03-coroutines-and-pipelining.cpp:205-217`).
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
* **Run**: `./build/examples/06-modules/redis/qb-example-modules-redis-coroutines-and-pipelining`

---

These examples provide a practical starting point for leveraging Redis with the QB C++ Actor Framework. 