# QB Core Framework Examples

This directory contains a collection of examples demonstrating various features and patterns of the QB Core Actor Framework (`qb-core`) and its underlying asynchronous I/O library (`qb-io`).

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

1.  **QB Framework**: The QB Actor Framework (specifically `qb-core` and `qb-io` modules) must be built and installed or available as CMake targets.
2.  **CMake**: CMake version 3.14 or higher is required to build the examples.
3.  **C++17 Compiler**: A C++17 compatible compiler (e.g., GCC 7+, Clang 5+).

## Building the Examples

The `CMakeLists.txt` file in this directory is configured to build all core examples. Each example links against the `qb-core` library (which implicitly includes `qb-io`).

1.  **Navigate to the QB build directory**: This is the directory where you've built the main QB framework (if building as part of QB).
    *Alternatively, if building standalone, navigate to `examples/core/`.*
2.  **Build a specific example** (if part of a larger QB build):
    ```bash
    # From your QB build directory
    cmake --build . --target <example_name>
    # e.g.,
    cmake --build . --target example1_simple_actor
    ```
3.  **Build all core examples** (if building standalone or as part of QB):
    ```bash
    # If building standalone:
    cd examples/core
    mkdir build
    cd build
    cmake .. 
    make # or your specific build system command (e.g., ninja)
    
    # If part of QB, building the qb-core target or all examples might suffice.
    ```

The executables will typically be placed in your CMake build system's binary output directory (e.g., `your_qb_build_dir/bin/` or `examples/core/build/`).

## Running the Examples

Once built, you can run each example directly from its location in the build output directory:
```bash
./<example_name>
# e.g.,
./example1_simple_actor
```

## Example Descriptions

### `example1_simple_actor.cpp`
*   **Focus**: Fundamental actor creation, message passing, and lifecycle.
*   **Actors**:
    *   `SimpleActor`: Receives `SimpleEvent`s and terminates after a count.
    *   `SenderActor`: Uses `qb::ICallback` to periodically send `SimpleEvent`s to `SimpleActor`.
*   **QB Features**: `qb::Actor`, `engine.addActor`, `onInit`, custom `qb::Event`, `registerEvent`, `on(EventType&)`, `push`, `kill`, `qb::ICallback`, `registerCallback`, `onCallback`, `qb::io::cout`.

### `example2_basic_actors.cpp`
*   **Focus**: Request-response communication pattern.
*   **Actors**:
    *   `ReceiverActor`: Listens for `MessageEvent`s, simulates work, sends `ResponseEvent` back to the source.
    *   `SenderActor` (Alice, Bob): Periodically send `MessageEvent`s, listen for `ResponseEvent`s.
*   **QB Features**: `event.getSource()`, managing multiple senders and a single receiver, state tracking for termination.

### `example3_multicore.cpp`
*   **Focus**: Distributing actors across multiple CPU cores and event broadcasting.
*   **Actors**:
    *   `WorkerActor`: Deployed on multiple cores, handles `HighPriorityEvent`, `StandardEvent`, `LowPriorityEvent` with different processing times. Receives `SystemNotificationEvent`.
    *   `DispatcherActor`: Runs on a specific core, dispatches work to workers round-robin, broadcasts `SystemNotificationEvent`s using `qb::BroadcastId(core_id)`.
*   **QB Features**: Multi-core assignment with `engine.addActor(core_id, ...)`, `qb::BroadcastId`, `getIndex()` for core ID.

### `example4_lifecycle.cpp`
*   **Focus**: Advanced actor lifecycle management using a supervisor-worker pattern.
*   **Actors**:
    *   `WorkerActor`: Can be started (`StartWorkEvent`), monitored (`StatusRequestEvent`), and stopped (`ShutdownRequestEvent`). Handles `qb::KillEvent` explicitly.
    *   `SupervisorActor`: Manages workers, sends start commands, polls status, and initiates coordinated shutdown.
*   **QB Features**: Supervisor-worker pattern, explicit `qb::KillEvent` handling, `unregisterCallback`, coordinated startup/shutdown sequences.

### `example5_timers.cpp`
*   **Focus**: Implementing timer-like behavior and delayed actions using self-messaging patterns.
*   **Actors**:
    *   `TimerManager`: Simulates a timer service, receiving `StartTimerMsg` and `CancelTimerMsg`. Uses self-sent `DelayedActionMsg` to trigger `TimerFiredMsg`.
    *   `Application`: Interacts with `TimerManager`, receives `TimerFiredMsg`, uses self-sent `DelayedActionMsg` to sequence its operations, and initiates system shutdown via `broadcast<qb::KillEvent>()`.
*   **QB Features**: Self-messaging for timed actions (alternative to `qb::io::async::callback`), `broadcast<qb::KillEvent>()`.

### `example6_shared_queue.cpp`
*   **Focus**: Producer-consumer pattern with actors interacting via an externally managed, thread-safe shared queue.
*   **Components**:
    *   `SharedQueue<WorkItemMsg>`: Custom thread-safe queue (std::mutex).
    *   `Producer` Actor: Pushes `WorkItemMsg` to the `SharedQueue`.
    *   `Consumer` Actors: Pop `WorkItemMsg` from the `SharedQueue` and process them.
    *   `Supervisor` Actor: Monitors queue size and consumer stats, initiates shutdown.
*   **QB Features**: Demonstrates integration of actors with external shared state, contrasting with pure message passing.

### `example7_pub_sub.cpp`
*   **Focus**: Implementing a publish-subscribe (pub/sub) messaging pattern.
*   **Actors**:
    *   `BrokerActor`: Central hub managing topic subscriptions (`SubscribeMessage`, `UnsubscribeMessage`) and distributing `PublishMessage` content as `MessageReceivedMessage` to subscribers.
    *   `MessagePublisher`: Publishes messages to topics via the `BrokerActor`.
    *   `SubscriberActor`: Receives messages for subscribed topics.
    *   `DemoController`: Orchestrates the demo, manages subscriptions, triggers publications, and requests stats/history.
*   **QB Features**: Decoupled messaging, dynamic subscriptions (simulated), `qb::io::async::callback()` for demo sequencing.

### `example8_state_machine.cpp`
*   **Focus**: Implementing a finite state machine (FSM) within an actor.
*   **Actors**:
    *   `CoffeeMachineActor`: Implements FSM logic (states: IDLE, SELECTING, PAYMENT, etc.). Transitions based on `InputEventMessage`. Uses `qb::io::async::callback()` for timed operations (brewing). Publishes `StateChangeMessage`.
    *   `UserInterfaceActor`: Simulates user interaction, sends `InputEventMessage`s, subscribes to `StateChangeMessage`, requests status.
*   **QB Features**: FSM logic encapsulation, `qb::io::async::callback()` for delayed self-events, state notifications.

### `example9_trading_system.cpp`
*   **Focus**: Simulating a basic multi-core financial trading system.
*   **Actors**:
    *   `ClientActor`: Generates and sends `NewOrderMessage`s, receives `ExecutionMessage`s.
    *   `OrderEntryActor`: Gateway for client orders, forwards to `MatchingEngineActor`.
    *   `MatchingEngineActor`: Core matching logic, maintains `OrderBook`s, generates `Trade`s, sends `TradeMessage` and `ExecutionMessage`.
    *   `MarketDataActor`: Receives `TradeMessage`, disseminates `MarketDataMessage`.
    *   `SupervisorActor`: Orchestrates the system, requests stats, manages lifecycle.
*   **QB Features**: Multi-core deployment for different components, complex actor interactions, state management for order books.

### `example10_distributed_computing.cpp`
*   **Focus**: Simulating a distributed task computing system with dynamic worker management and load balancing.
*   **Actors**:
    *   `TaskGeneratorActor`: Creates `Task` objects and sends them as `TaskMessage`s.
    *   `TaskSchedulerActor`: Receives tasks, assigns them to available `WorkerNodeActor`s based on metrics, handles `WorkerHeartbeatMessage` and `WorkerStatusMessage`.
    *   `WorkerNodeActor`: Executes assigned tasks, simulates processing time based on task complexity, reports results (`ResultMessage`) and status.
    *   `ResultCollectorActor`: Aggregates `TaskResult`s.
    *   `SystemMonitorActor`: Oversees the system, requests and displays `SystemStatsMessage`, manages worker lifecycle via `UpdateWorkersMessage`, and initiates shutdown.
*   **QB Features**: Dynamic actor management (conceptual, workers are pre-started but scheduler manages assignment), load balancing concepts, comprehensive system monitoring, `qb::string<N>` for efficient string usage in events/structs.

---

These examples provide a solid foundation for understanding and utilizing the QB Core Actor Framework. Explore the source code of each example to see the concepts in action. Remember that these are illustrative and real-world applications would involve more robust error handling, configuration, and domain-specific logic. 