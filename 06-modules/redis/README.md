# QB Redis Module (`qbm-redis`) Examples

This directory contains examples demonstrating various features of the `qbm-redis` module, which provides asynchronous
Redis client functionality integrated with the QB C++ Actor Framework.

## Prerequisites

- **A running Redis server instance — all ten need one.** `dev/agent/run-examples.py` records
  `needs = redis` for every target here and reports a SKIP, never a pass, when nothing answers.
- The QB Framework, including `qb-core`, `qb-io`, and `qbm-redis`, must be built.
- Eight of the ten define `#define REDIS_URI {"tcp://localhost:6379"}` near the top of the `.cpp`;
  edit it if your server is elsewhere. The two exceptions spell the URI inline:
  `03-coroutines-and-pipelining.cpp` repeats `{"tcp://localhost:6379"}` per client, and
  `09-reliability.cpp` uses named constants `URI` / `DEAD_URI` (`:78-79`) — the second deliberately
  points at a port where nothing listens.

## Building the Examples

From the superproject root, which force-enables `QB_BUILD_EXAMPLES`:

```bash
cmake --preset release
cmake --build --preset release --target qb-example-modules-redis-connect
```

The executables land in `build/presets/release/examples/06-modules/redis/`. The three
pre-3.0 programs that used to sit beside them — `example2_hash_operations`,
`example3_list_operations` and `example8_complex_actor_system` — have been RETIRED:
`02-data-types` merges the first two and adds sets, and `07-scripting` + `10-cache-actor`
between them cover the third.

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
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-connect`

---

### 2. Data Types (`02-data-types.cpp`) — **the merge, plus sets**

* **Purpose**: string, hash, list and **set** in one program, and the question the older pair never
  answered: which one to reach for. The organising idea is that the C++ TYPE of each command's
  `Reply` states the semantics — `get` → `optional<string>` (may be ABSENT), `hgetall` →
  `unordered_map` (an object, whole), `lrange` → `vector` (order is part of the value), `smembers` →
  `unordered_set` (no order, no duplicates), `sismember` → `bool` (the question, not the set).
* **QB/QBM Redis Features**: `set`/`get`/`append`/`strlen`/`incrby`/`mset`/`mget`;
  `hset`/`hget`/`hgetall`/`hincrby`/`hexists`/`hdel`/`hlen`; `rpush`/`lpush`/`lrange`/`llen`/`ltrim`/
  `lpop`/`rpop`/`lindex`/`lset`/`blpop`; `sadd`/`smembers`/`sismember`/`scard`/`sinter`/`sunion`/
  `sdiff`/`srem`; and `hkeys`/`hvals`. Sets had no demonstrator anywhere in the corpus before this
  file, and `hkeys`, `hvals`, `lindex`, `lset` and `blpop` were added when the two pre-3.0 programs
  were retired: they were the five commands the merge had left described in prose and demonstrated
  by no file.
* **One measured gotcha**: `qb::unordered_set` is the vendored ska flat hash set and predates C++20's
  `contains` — use `count(k) == 1`.
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-data-types`

---


### 3. Coroutine API without Actors (`03-coroutines-and-pipelining.cpp`)

* **@example qbm-redis: Coroutine API on pure qb-io — NO ACTORS**
* **Purpose**: The smallest complete picture of the coroutine API. Commands are issued directly on
  `qb::redis::tcp::client` and `co_await`ed; there is no wrapper class, no callback, and no actor system — the driver
  is `qb::io::async::init()` + `coro_scheduler().spawn(...)` + `run_until(running)` — the three calls at `:207`, `:212` and `:213` (`03-coroutines-and-pipelining.cpp:205-217`).
* **Key Components**: four free coroutines called in sequence from `run_all_examples()` (`:184-202`):
    1. `example_simple_get_set()` — `co_await redis.connect()`, `set`, `get`; `get()` yields
       `Reply<std::optional<std::string>>` (`:37-63`).
    2. `example_sequential_operations()` — a hash written field by field, then read back (`:69-97`).
    3. `example_multiple_keys()` — three `GET`s issued **in parallel** with
       `co_await qb::io::async::when_all(...)` (`:133`). `when_all` takes real `task<T>` objects, not raw command
       awaiters, so each `GET` is wrapped in a one-line local coroutine; because they share one client they are
       pipelined into a single round-trip group rather than three (`:103-148`).
    4. `example_error_handling()` — the miss case: `ok()` true, `result()` empty, for a key that does not exist
       (`:154-175`).
* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `co_await client.<command>()`, `Reply<T>::ok()` / `result()`,
  `qb::io::async::when_all`, and the standalone `run_until` scaffolding.
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-coroutines-and-pipelining`

---

### 4. Publish/Subscribe Example (`04-pubsub.cpp`)

* **@example qbm-redis: Publish/Subscribe Messaging with Actors**
* **Purpose**: Implements a basic chat-like system using Redis Pub/Sub capabilities for real-time messaging between
  actors.
* **Key Components**: `PublisherActor`, `SubscriberActor` (using `qb::redis::tcp::co_consumer`), `CoordinatorActor`.
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `co_await client.publish(channel, message)`
  (`04-pubsub.cpp:173`), and **`qb::redis::tcp::co_consumer`** — the *coroutine* Pub/Sub consumer
  (`:208`) — with `co_await consumer.subscribe(channel)` → `Reply<qb::redis::subscription>` (`:289`) and a
  `while (auto msg = co_await _consumer.receive()) { ... }` loop (`:259`) instead of a message callback. Read that
  loop's capture list: it takes `this` only for the consumer it awaits, and copies everything it *reads*
  (`name = _name`, `coordinator = _coordinator_id`) before the first `co_await`. The loop resumes when
  `~RedisCoroConsumer` closes the message channel — i.e. while the actor is being destroyed — so a member read after
  the resume is a use-after-free, and was one.
  Both consumers are real types (`qbm/redis/src/qbm/redis/redis.h:1705-1706`): `cb_consumer` is the
  callback-driven one, `co_consumer` the coroutine one. This example uses `co_consumer`.
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-pubsub`

---

### 5. Transactions (`05-transactions.cpp`) — **rewritten**

Titled *Transactions and Atomic Operations*, 716 lines, and — measured — calling no `MULTI`,
`EXEC`, `WATCH` or `DISCARD`. It was the one file in the restructured corpus whose filename was not
true of it. It now exercises all five verbs against a live Redis, with a gated verdict for each,
plus the trap that decides whether a MULTI block works: a queued command answers `+QUEUED`, and a
TYPED client reacts three different ways to that — `status` gives `ok()==true` with the string
`QUEUED`, `long long` gives `ok()==false` for a perfectly queued command, and
`std::optional<std::string>` gives `ok()==true` with the value literally set to `"QUEUED"`, which
is the silent one. It also shows how to tell an aborted `EXEC` (RESP nil, `raw()->is_null()`) from
a genuine parse error, and how to read a heterogeneous batch through `raw()`.

* **QB/QBM Redis Features**: `qb::redis::tcp::client`, `multi`, `exec<std::string>`, `discard`,
  `watch`, `unwatch`, `is_in_multi`. It is 270 lines with **no actor at all** — a `qb::io::async`
  driver and one client, so the transaction is the whole subject.
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-transactions`

---

### 6. Stream Processor (`06-streams.cpp`) — **rewritten**

* **@example qbm-redis: Redis Streams, consumer groups, and the difference between them**
* **Purpose**: the two stream semantics side by side. WITHIN the `workers` group two competing
  consumers SPLIT the 40 entries (the work-queue semantic); ACROSS groups the `audit` group gets its
  own independent copy of all 40 (the fan-out semantic). Then `XACK` empties the pending list,
  a plain `XREAD` reads the same entries with no group and no acknowledgement at all, and `XTRIM`
  bounds the stream.
* **Key Components**: `SensorProducerActor` (`xadd`), `StreamConsumerActor` (`xgroup_create`,
  `xreadgroup`, `xack`), `CoordinatorActor` (`xpending`, `xread`, `xlen`, `xtrim`, `del`).
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::BroadcastId`, `spawn` +
  `qb::ScopedCoroContext`, `qb::redis::tcp::client`. Multi-core deployment across four cores.
* **Why it was rewritten rather than tuned** — it was the corpus's only failing example, and the
  three defects were measured, not guessed. It **acknowledged nothing, ever**: its consumer walked
  the `xreadgroup` reply with a hard-coded nesting that matched no shape the server sends, so `xack`
  was never called (`entries-read 1021020, pending 1021020` in both groups, and 8 printed lines from
  two consumers in a 300 s run). It **spawned a coroutine per turn of the event loop**, so nothing
  bounded how many commands were in flight on one connection, and the stream overshot its own
  target. And it deleted a FIXED million-entry key at startup, charging that O(N) server work to the
  next run — which is why the same program timed 43 s, 74 s, 150 s and 300 s-without-finishing on
  the same machine. It now writes 40 entries to a key unique to the run, reads the reply
  structurally, runs one coroutine per actor, and deletes its key: **0.2–0.3 s, five runs of five.**
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-streams`

---

### 7. Scripting (`07-scripting.cpp`) — **new**

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
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-scripting`

---

### 8. Sorted Sets and Expiry (`08-sorted-sets-and-ttl.cpp`) — **new**

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
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-sorted-sets-and-ttl`

---

### 9. Reliability (`09-reliability.cpp`) — **new**

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
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-reliability`

---


### 10. Asynchronous Operations with Actors (`10-cache-actor.cpp`)

* **@example qbm-redis: Asynchronous Operations within QB Actors (Coroutine API)**
* **Purpose**: Illustrates how `qbm-redis` is integrated into a QB actor system, featuring a worker actor performing
  Redis operations with the coroutine API.
* **Key Components**: `RedisWorkerActor` (performs Redis operations), `MainActor` (coordinates).
* **QB/QBM Redis Features**: `qb::Actor`, `qb::Main`, `qb::Event`, and `qb::redis::tcp::client` inside an actor.
  Nothing here is a blocking call: `onInit()` is a `qb::io::async::task<bool>` that does `co_await _redis.connect()`,
  and each `RedisDataEvent` spawns a coroutine that does `co_await _redis.set(...)`, `co_await _redis.incr(...)`,
  `co_await _redis.get(...)`, reading `Reply<T>::ok()` / `result()` between steps
  (`10-cache-actor.cpp:114-115`, `:125`, `:163`, `:168-176`).
* **Run**: `./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-cache-actor`

---

These examples provide a practical starting point for leveraging Redis with the QB C++ Actor Framework. 