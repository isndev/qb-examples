# QB Core Framework Examples

This directory contains a collection of examples demonstrating various features and patterns of the QB Core Actor
Framework (`qb-core`) and its underlying asynchronous I/O library (`qb-io`).

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Building the Examples](#building-the-examples)
- [Running the Examples](#running-the-examples)
- [Example Descriptions](#example-descriptions)
    - [`example1_simple_actor.cpp`](#example1_simple_actorcpp)
    - [`example2_basic_actors.cpp`](#example2_basic_actorscpp)
    - [`example3_multicore.cpp`](#example3_multicorecpp)
    - [`example4_lifecycle.cpp`](#example4_lifecyclecpp)
    - [`example5_timers.cpp`](#example5_timerscpp)
    - [`example6_shared_queue.cpp`](#example6_shared_queuecpp)
    - [`example7_pub_sub.cpp`](#example7_pub_subcpp)
    - [`example8_state_machine.cpp`](#example8_state_machinecpp)
    - [`example9_trading_system.cpp`](#example9_trading_systemcpp)
    - [`example10_distributed_computing.cpp`](#example10_distributed_computingcpp)

## Overview

These examples are designed to illustrate fundamental and advanced concepts of the QB Actor Framework, such as:

- Actor creation, communication, and lifecycle management.
- Event-driven programming and custom event types.
- Multi-core actor distribution and concurrency.
- Periodic tasks and delayed actions using callbacks and self-messaging.
- Common actor patterns like supervisor-worker, publish-subscribe, and finite state machines.
- Integration with asynchronous I/O operations.

Each example is a self-contained C++ application that uses `qb::Main` to orchestrate the actors.

## Prerequisites

1. **QB Framework**: The QB Actor Framework (specifically `qb-core` and `qb-io` modules) must be built and installed or
   available as CMake targets.
2. **CMake**: CMake version 3.14 or higher is required to build the examples.
3. **C++17 Compiler**: A C++17 compatible compiler (e.g., GCC 7+, Clang 5+).

## Building the Examples

The `CMakeLists.txt` file in this directory is configured to build all core examples. Each example links against the
`qb-core` library (which implicitly includes `qb-io`).

1. **Navigate to the QB build directory**: This is the directory where you've built the main QB framework (if building
   as part of QB).
   *Alternatively, if building standalone, navigate to `examples/core/`.*
2. **Build a specific example** (if part of a larger QB build):
   ```bash
   # From your QB build directory
   cmake --build . --target <example_name>
   # e.g.,
   cmake --build . --target example1_simple_actor
   ```
3. **Build all core examples** (if building standalone or as part of QB):
   ```bash
   # If building standalone:
   cd examples/core
   mkdir build
   cd build
   cmake .. 
   make # or your specific build system command (e.g., ninja)
   
   # If part of QB, building the qb-core target or all examples might suffice.
   ```

The executables will typically be placed in your CMake build system's binary output directory (e.g.,
`your_qb_build_dir/bin/` or `examples/core/build/`).

## Running the Examples

Once built, you can run each example directly from its location in the build output directory:

```bash
./<example_name>
# e.g.,
./example1_simple_actor
```

## Example Descriptions

### `example1_simple_actor.cpp`

* **Focus**: Fundamental actor creation, message passing, and lifecycle.
* **Actors**:
    * `SimpleActor`: Receives `SimpleEvent`s and terminates after a count.
    * `SenderActor`: Uses `qb::ICallback` to send a `SimpleEvent` to `SimpleActor` on every turn of the event loop.
* **QB Features**: `qb::Actor`, `engine.addActor`, `onInit`, custom `qb::Event`, `registerEvent`, `on(EventType&)`,
  `push`, `kill`, `qb::ICallback`, `registerCallback`, `on(qb::LoopEvent const&)`, `qb::io::cout`.

> `qb::ICallback` is the **every-turn** hook, not a timer. For work that must happen after a delay, use
> `spawn(...)` + `co_await ctx.sleep(d)` (examples 2-10) and never `std::this_thread::sleep_for`, which freezes
> every actor on the core.

### `example2_basic_actors.cpp`

* **Focus**: Request-response communication pattern.
* **Actors**:
    * `ReceiverActor`: Listens for `MessageEvent`s, simulates work **asynchronously**, sends `ResponseEvent` back to
      the source.
    * `SenderActor` (Alice, Bob): Pace their `MessageEvent`s with a coroutine timer, listen for `ResponseEvent`s.
* **QB Features**: `event.getSource()`, `spawn(...)` + `co_await ctx.sleep(...)` for a non-blocking delay,
  `ctx.push<T>()` to return to actor context, state tracking for termination.

### `example3_multicore.cpp`

* **Focus**: Distributing actors across multiple CPU cores and event broadcasting.
* **Actors**:
    * `WorkerActor`: Deployed on multiple cores, handles `HighPriorityEvent`, `StandardEvent`, `LowPriorityEvent` with
      different processing times. Receives `SystemNotificationEvent`.
    * `DispatcherActor`: Runs on a specific core, dispatches work to workers round-robin, sends
      `SystemNotificationEvent`s system-wide with `broadcast<T>()`.
* **QB Features**: Multi-core assignment with `engine.addActor(core_id, ...)`, `broadcast<T>()` **versus**
  `qb::BroadcastId(core_id)` (which reaches one core's actors only), `getIndex()` for core ID.

### `example4_lifecycle.cpp`

* **Focus**: Advanced actor lifecycle management using a supervisor-worker pattern.
* **Actors**:
    * `WorkerActor`: Can be started (`StartWorkEvent`), monitored (`StatusRequestEvent`), and stopped (
      `ShutdownRequestEvent`). Handles `qb::KillEvent` explicitly.
    * `SupervisorActor`: Manages workers, sends start commands, polls status, and initiates coordinated shutdown.
* **QB Features**: Supervisor-worker pattern, two-phase shutdown (drain with `ShutdownRequestEvent`, then terminate
  with `broadcast<qb::KillEvent>()`), explicit `qb::KillEvent` handling via `registerEvent<qb::KillEvent>(*this)`,
  coordinated startup/shutdown sequences.

### `example5_timers.cpp`

* **Focus**: Real timers and delayed actions, and choosing between the four mechanisms qb offers.
* **Actors**:
    * `TimerManager`: A timer service. Arms each interval with `spawn(...)` + `co_await ctx.sleep(interval)` and
      answers the requester with `TimerFiredMsg`; `CancelTimerMsg` stops one.
    * `Application`: Interacts with `TimerManager`, receives `TimerFiredMsg`, sequences its own steps with the same
      mechanism, and initiates system shutdown via `broadcast<qb::KillEvent>()`.
* **QB Features**: `spawn(...)` + `co_await ctx.sleep(...)`, `event.getSource()`, `broadcast<qb::KillEvent>()`. The
  file header contrasts all four ways to do something later: `ctx.sleep`, `qb::io::async::callback(f, d)`,
  `qb::io::async::defer(f)` and `qb::ICallback`.

### `example6_shared_queue.cpp`

* **Focus**: Producer-consumer pattern with actors interacting via an externally managed, thread-safe shared queue.
* **Components**:
    * `SharedQueue<WorkItem>`: Custom thread-safe queue (std::mutex) of plain values, not events.
    * `Producer` Actor: Pushes `WorkItem`s to the `SharedQueue`.
    * `Consumer` Actors: Pop `WorkItem`s from the `SharedQueue` and process them.
    * `Supervisor` Actor: Monitors queue size and consumer stats, initiates shutdown.
* **QB Features**: Demonstrates integration of actors with external shared state, contrasting with pure message passing.

> **This one is a counter-example, not a pattern.** A mutex-protected container shared by five actors is the opposite
> of what the rest of this directory teaches, and the file's own header lists what it costs you (no back-pressure, a
> lock on the hot path, polling instead of delivery). Read it for the seam where an actor system meets code you did
> not write.

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

### `example8_state_machine.cpp`

* **Focus**: Implementing a finite state machine (FSM) within an actor.
* **Actors**:
    * `CoffeeMachineActor`: Implements FSM logic (states: IDLE, SELECTING, PAYMENT, etc.). Transitions based on
      `InputEventMessage`. Uses `spawn(...)` + `co_await ctx.sleep(...)` for timed operations (brewing). Publishes
      `StateChangeMessage`.
    * `UserInterfaceActor`: Simulates user interaction, sends `InputEventMessage`s, subscribes to `StateChangeMessage`,
      requests status.
* **QB Features**: FSM logic encapsulation, `spawn(...)` + `co_await ctx.sleep(...)` for delayed self-events (bound
  to the actor's lifetime, unlike `qb::io::async::callback(f, d)`), state notifications.

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