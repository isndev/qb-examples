# Pre-3.0 holding directory — four core examples awaiting their replacements

**This is not a tier.** The corpus is organised by level now (see
[`examples/README.md`](../README.md)); six of the ten programs that used to live here moved to
[`01-actors/`](../01-actors/README.md), where their descriptions moved with them.

The four below did not move, and the reason is the same for each: the architecture **retires**
them, and a retirement only lands together with its replacement. Retiring one now would leave the
corpus promising something no file delivers — the exact defect the tier structure exists to make
impossible. They keep their pre-3.0 hand-written target names on purpose: a derived name would put
them in a tier, and they are not in one.

| Still here | To be replaced by | Why |
|---|---|---|
| `example6_shared_queue.cpp` | `01-actors/03-event-payloads` | 377 lines of `std::mutex` in the directory whose job is to teach that actors do not share state; its own header concedes it "demonstrates integration, not a QB-specific pattern". The replacement teaches the same foreign-thread problem with `lockfree::spsc::ringbuffer`, a qb API with zero coverage today. |
| `example7_pub_sub.cpp` | `04-patterns/01-pubsub` | 969 lines hand-rolling what `qb::PubSub<Topic>` ships — a class this file names in a comment and then does not use. |
| `example9_trading_system.cpp` | `07-applications/03-market-data-hub` | 1490 lines: a program of that size and ambition belongs in the applications tier, where its multi-core matching-engine shape becomes the corpus's one non-web application. |
| `example10_distributed_computing.cpp` | `04-patterns/03-worker-pool` + `04-patterns/04-scatter-gather` | 1320 lines hand-rolling both, one of them assigning half the fleet no work at all. |

When the last replacement lands, this directory goes with it.

## Building and running

```bash
cmake --preset release
cmake --build --preset release --target example7_pub_sub
./build/presets/release/examples/core/example7_pub_sub
```

## Example descriptions

### `example6_shared_queue.cpp`

* **Focus**: Producer-consumer pattern with actors interacting via an externally managed, thread-safe shared queue.
* **Components**:
    * `SharedQueue<WorkItem>`: Custom thread-safe queue (std::mutex) of plain values, not events.
    * `Producer` Actor: Pushes `WorkItem`s to the `SharedQueue`.
    * `Consumer` Actors: Pop `WorkItem`s from the `SharedQueue` and process them.
    * `Supervisor` Actor: Monitors queue size and consumer stats, initiates shutdown.
* **QB Features**: Demonstrates integration of actors with external shared state, contrasting with pure message passing.

> **This one is a counter-example, not a pattern.** A mutex-protected container shared by five actors is the opposite
> of what the rest of this directory teaches, and the file's own header lists what it costs you
> (`example6_shared_queue.cpp:32-38`): back-pressure disappears, the queue is a lock on a hot path taken by four
> different actors, and nothing in the type system stops you reaching further into shared state later. Read it for
> the seam where an actor system meets code you did not write.

### `example7_pub_sub.cpp`

* **Focus**: Implementing a publish-subscribe (pub/sub) messaging pattern.
* **Actors**:
    * `BrokerActor`: Central hub managing topic subscriptions (`SubscribeMessage`, `UnsubscribeMessage`) and
      distributing `PublishMessage` content as `MessageReceivedMessage` to subscribers.
    * `MessagePublisher`: Publishes messages to topics via the `BrokerActor`.
    * `SubscriberActor`: Receives messages for subscribed topics.
    * `DemoController`: Orchestrates the demo, manages subscriptions, triggers publications, and requests stats/history.
* **QB Features**: Decoupled messaging, dynamic subscriptions (simulated), one-shot `registerCallback` /
  `unregisterCallback`, `spawn(...)` + `co_await ctx.sleep(...)` for demo sequencing. `qb/core/patterns/pubsub.h`
  ships a tested broker that makes most of this file unnecessary in real code.

### `example9_trading_system.cpp`

* **Focus**: Simulating a basic multi-core financial trading system.
* **Actors**:
    * `ClientActor`: Generates and sends `NewOrderMessage`s, receives `ExecutionMessage`s.
    * `OrderEntryActor`: Gateway for client orders, forwards to `MatchingEngineActor`.
    * `MatchingEngineActor`: Core matching logic, maintains `OrderBook`s, generates `Trade`s, sends `TradeMessage` and
      `ExecutionMessage`.
    * `MarketDataActor`: Receives `TradeMessage`, disseminates `MarketDataMessage`.
    * `SupervisorActor`: Orchestrates the system, requests stats, manages lifecycle.
* **QB Features**: Multi-core deployment for different components, complex actor interactions, state management for
  order books.

### `example10_distributed_computing.cpp`

* **Focus**: Simulating a distributed task computing system with dynamic worker management and load balancing.
* **Actors**:
    * `TaskGeneratorActor`: Creates `Task` objects and sends them as `TaskMessage`s.
    * `TaskSchedulerActor`: Receives tasks and assigns them round-robin to the `WorkerNodeActor`s it knows to be idle,
      requeueing anything a worker rejects; handles `WorkerHeartbeatMessage` and `WorkerStatusMessage`.
    * `WorkerNodeActor`: Executes assigned tasks, simulates processing time based on task complexity, reports results (
      `ResultMessage`) and status.
    * `ResultCollectorActor`: Aggregates `TaskResult`s.
    * `SystemMonitorActor`: Oversees the system, polls each component for the counters it owns and displays the merged
      figures, wires the worker list into the scheduler via `UpdateWorkersMessage`, and initiates shutdown.
* **QB Features**: Dynamic actor management (conceptual, workers are pre-started but scheduler manages assignment), load
  balancing, comprehensive system monitoring, `qb::string<N>` for efficient string usage in events/structs, and
  telemetry by request/report events rather than shared counters.

---

These examples provide a solid foundation for understanding and utilizing the QB Core Actor Framework. Explore the
source code of each example to see the concepts in action. Remember that these are illustrative and real-world
applications would involve more robust error handling, configuration, and domain-specific logic.

## Checking your own qb program

Exit code 0 proves very little about an actor system: an event pushed to a default-constructed `qb::ActorId`, a
handler that was never subscribed, a timer that outlives its actor -- none of them fail loudly. Two habits catch
almost all of it:

1. **Diff the run against the program's own `printf`s.** Every path a file claims to exercise should print
   something; a line that never appears is a dead path. That single check found thirteen of them across this corpus.
2. **Run it under the sanitizers**, not just release:

   ```bash
   cmake --preset sanitize && cmake --build --preset sanitize
   ./build/presets/sanitize/examples/core/example9_trading_system
   ```

   `sanitize` is ASan + UBSan; `sanitize-thread` is TSan and is what finds a cross-core data race. Two examples in
   this directory used to abort under `sanitize` while exiting 0 in release.
