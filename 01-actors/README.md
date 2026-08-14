# Tier 01 — the actor model

Actors, events, cores, lifetime. No socket, no protocol, no module. This tier has no prerequisite
of its own; every later tier has it.

The CMake target and the binary are **derived** from each file's path
(`examples/cmake/qbExample.cmake`), so neither is written down anywhere:

| Source | Target and binary |
|---|---|
| `01-hello-actor.cpp` | `qb-example-actors-hello-actor` |
| `02-messaging.cpp` | `qb-example-actors-messaging` |
| `04-cores-and-placement.cpp` | `qb-example-actors-cores-and-placement` |
| `05-lifecycle.cpp` | `qb-example-actors-lifecycle` |
| `06-doing-things-later.cpp` | `qb-example-actors-doing-things-later` |
| `09-state-machine.cpp` | `qb-example-actors-state-machine` |

**The holes are the tier's to-do list, not an accident.** `03-event-payloads`, `07-service-actor`,
`08-child-actors`, `10-signals-and-shutdown` and `11-hot-path` have no program yet — between them
they are why `ServiceActor`, `getService`, `is_actor_alive`, `send<>`, `getPipe`, `build_event`
and `to()` have zero demonstrators anywhere in the corpus.

```bash
cmake --preset release
cmake --build --preset release --target qb-example-actors-hello-actor
./build/presets/release/examples/01-actors/qb-example-actors-hello-actor
```

Each program's own header block states what it teaches, what it demonstrates, what it assumes you
have read, and what it prints. `dev/agent/check-example-headers.py` checks the middle two against
the file's code.

## Example descriptions

### `01-hello-actor.cpp`

* **Focus**: Fundamental actor creation, message passing, and lifecycle.
* **Actors**:
    * `SimpleActor`: Receives `SimpleEvent`s and terminates after a count.
    * `SenderActor`: Uses `qb::ICallback` to send a `SimpleEvent` to `SimpleActor` on every turn of the event loop.
* **QB Features**: `qb::Actor`, `engine.addActor`, `onInit`, custom `qb::Event`, `registerEvent`, `on(EventType&)`,
  `push`, `kill`, `qb::ICallback`, `registerCallback`, `on(qb::LoopEvent const&)`, `qb::io::cout`.

> `qb::ICallback` is the **every-turn** hook, not a timer. For work that must happen after a delay, use
> `spawn(...)` + `co_await ctx.sleep(d)` (examples 2-10) and never `std::this_thread::sleep_for`, which freezes
> every actor on the core.

### `02-messaging.cpp`

* **Focus**: Request-response communication pattern.
* **Actors**:
    * `ReceiverActor`: Listens for `MessageEvent`s, simulates work **asynchronously**, sends `ResponseEvent` back to
      the source.
    * `SenderActor` (Alice, Bob): Pace their `MessageEvent`s with a coroutine timer, listen for `ResponseEvent`s.
* **QB Features**: `event.getSource()`, `spawn(...)` + `co_await ctx.sleep(...)` for a non-blocking delay,
  `ctx.push<T>()` to return to actor context, state tracking for termination.

### `04-cores-and-placement.cpp`

* **Focus**: Distributing actors across multiple CPU cores and event broadcasting.
* **Actors**:
    * `WorkerActor`: Deployed on multiple cores, handles `HighPriorityEvent`, `StandardEvent`, `LowPriorityEvent` with
      different processing times. Receives `SystemNotificationEvent`.
    * `DispatcherActor`: Runs on a specific core, dispatches work to workers round-robin, sends
      `SystemNotificationEvent`s system-wide with `broadcast<T>()`.
* **QB Features**: Multi-core assignment with `engine.addActor(core_id, ...)`, `broadcast<T>()` **versus**
  `qb::BroadcastId(core_id)` (which reaches one core's actors only), `getIndex()` for core ID.

### `05-lifecycle.cpp`

* **Focus**: Advanced actor lifecycle management using a supervisor-worker pattern.
* **Actors**:
    * `WorkerActor`: Can be started (`StartWorkEvent`), monitored (`StatusRequestEvent`), and stopped (
      `ShutdownRequestEvent`). Handles `qb::KillEvent` explicitly.
    * `SupervisorActor`: Manages workers, sends start commands, polls status, and initiates coordinated shutdown.
* **QB Features**: Supervisor-worker pattern, two-phase shutdown (drain with `ShutdownRequestEvent`, then terminate
  with `broadcast<qb::KillEvent>()`), explicit `qb::KillEvent` handling via `registerEvent<qb::KillEvent>(*this)`,
  coordinated startup/shutdown sequences.

### `06-doing-things-later.cpp`

* **Focus**: Real timers and delayed actions, and choosing between the four mechanisms qb offers.
* **Actors**:
    * `TimerManager`: A timer service. Arms each interval with `spawn(...)` + `co_await ctx.sleep(interval)` and
      answers the requester with `TimerFiredMsg`; `CancelTimerMsg` stops one.
    * `Application`: Interacts with `TimerManager`, receives `TimerFiredMsg`, sequences its own steps with the same
      mechanism, and initiates system shutdown via `broadcast<qb::KillEvent>()`.
* **QB Features**: `spawn(...)` + `co_await ctx.sleep(...)`, `event.getSource()`, `broadcast<qb::KillEvent>()`. The
  file header contrasts all four ways to do something later: `ctx.sleep`, `qb::io::async::callback(f, d)`,
  `qb::io::async::defer(f)` and `qb::ICallback`.

### `09-state-machine.cpp`

* **Focus**: Implementing a finite state machine (FSM) within an actor.
* **Actors**:
    * `CoffeeMachineActor`: Implements FSM logic (states: IDLE, SELECTING, PAYMENT, etc.). Transitions based on
      `InputEventMessage`. Uses `spawn(...)` + `co_await ctx.sleep(...)` for timed operations (brewing). Publishes
      `StateChangeMessage`.
    * `UserInterfaceActor`: Simulates user interaction, sends `InputEventMessage`s, subscribes to `StateChangeMessage`,
      requests status.
* **QB Features**: FSM logic encapsulation, `spawn(...)` + `co_await ctx.sleep(...)` for delayed self-events (bound
  to the actor's lifetime, unlike `qb::io::async::callback(f, d)`), state notifications.
