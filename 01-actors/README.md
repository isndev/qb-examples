# Tier 01 — the actor model

Actors, events, cores, lifetime. No socket, no protocol, no module. This tier has no prerequisite
of its own; every later tier has it.

The CMake target and the binary are **derived** from each file's path
(`examples/cmake/qbExample.cmake`), so neither is written down anywhere:

| Source | Target and binary |
|---|---|
| `01-hello-actor.cpp` | `qb-example-actors-hello-actor` |
| `02-messaging.cpp` | `qb-example-actors-messaging` |
| `03-event-payloads.cpp` | `qb-example-actors-event-payloads` |
| `04-cores-and-placement.cpp` | `qb-example-actors-cores-and-placement` |
| `05-lifecycle.cpp` | `qb-example-actors-lifecycle` |
| `06-doing-things-later.cpp` | `qb-example-actors-doing-things-later` |
| `07-service-actor.cpp` | `qb-example-actors-service-actor` |
| `08-child-actors.cpp` | `qb-example-actors-child-actors` |
| `09-state-machine.cpp` | `qb-example-actors-state-machine` |
| `10-signals-and-shutdown.cpp` | `qb-example-actors-signals-and-shutdown` |
| `11-hot-path.cpp` | `qb-example-actors-hot-path` |

**The tier is now dense**, and the five programs that filled its holes are the ones that closed
the corpus's third-largest measured gap: `ServiceActor`, `getService`, `getServiceId`,
`is_actor_alive`, `addRefActor`, `send<>`, `getPipe`, `allocated_push`, `build_event`, `to()`,
`no_default_events`, `registerSignal` and the `lockfree::spsc::ringbuffer` bridge all had **zero**
demonstrators anywhere before them.

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

### `03-event-payloads.cpp`

* **Focus**: the one rule about event payloads a Mac cannot show you — an event is RELOCATED with
  `memcpy` and its source destructor never runs, so no member may STORE a pointer into itself.
* **Actors**: `Producer`/`Consumer` on two cores (the payload really crosses), and a `Bridge` that
  drains a lock-free ring fed by an ordinary `std::thread`.
* **QB Features**: `qb::string<N>`, `qb::ActorId`, a `shared_ptr`-boxed body, `qb::FillEvent<int>`,
  `qb::lockfree::spsc::ringbuffer<T, N>`, `qb::ICallback` + `on(qb::LoopEvent const&)`,
  `unregisterCallback`. It opens with a measurement — printed, not asserted — of which candidate
  payload types keep a pointer inside themselves on YOUR standard library.

### `07-service-actor.cpp`

* **Focus**: the framework's standard bootstrap object. One singleton per core per tag, reachable
  by TYPE with no id plumbing at all.
* **Actors**: `ConfigService` (on two cores, with different contents), a core-0-only
  `TelemetryService`, two `Worker`s whose constructors take nothing, and a `Reporter`.
* **QB Features**: `qb::ServiceActor<Tag>`, `qb::Service`, `getService<T>()` (including its
  `nullptr`, and the fact that it is the one lookup NOT phase-gated), and `getServiceId<Tag>(core)`
  — which addresses the service on ANOTHER core without a lookup, because a service's id is
  computed from its tag.

### `08-child-actors.cpp`

* **Focus**: actor trees, and the property the word "child" gets wrong.
* **Actors**: a `Team` that creates four `Member`s at runtime, and a `Watcher` that measures what
  survives the team.
* **QB Features**: `addRefActor` / `addRefHandle`, `qb::ActorHandle<T>` (`id()` usable at once,
  `get()` only for an ACTIVE actor on this thread), `is_actor_alive` as the thing that keeps a
  parent's registry bounded — and the measurement that killing a parent leaves its "children"
  running, because `addRefActor` makes a peer, not a subobject.

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

### `10-signals-and-shutdown.cpp`

* **Focus**: the first thing anyone needs to ship a server — and the corpus got it wrong three
  different ways before this.
* **Actors**: a `Server` that reloads on SIGHUP and drains on SIGTERM, and a `Ticker` that raises
  both against its own process so the program demonstrates itself.
* **QB Features**: `qb::SignalEvent` and the `registerEvent<qb::SignalEvent>` that makes a derived
  handler run; `qb::Main::registerSignal` / `unregisterSignal` / `ignoreSignal`; `qb::Main::stop`;
  and the exit-code contract — including the measured reason `hasError()` is only half of it
  (an ASYNC `onInit` failure sets no engine flag at all). Run it with `--fail-bind` to watch the
  exit code change from 0 to 1.

### `11-hot-path.cpp`

* **Focus**: the performance knobs qb is sold on, measured rather than asserted.
* **Actors**: a `Bench`, two `Sink`s (one per core) that opt OUT of the default event
  registrations, and a `Reporter`.
* **QB Features**: `send<>` vs `push<>`, `getPipe` + `allocated_push` for a variable-length event,
  `build_event`, `to()` / `EventBuilder`, `qb::no_default_events`, `qb::EventQOS0`, `setLatency`,
  `setAffinity` + `qb::CPU::ThreadPinningSupported()`, and `qb::tsc_ticks()`.

> Two of its printed findings are worth reading even if you never need the knobs: enqueue cost is
> the SAME same-core and cross-core (the boundary is paid at the flush, not at your call site),
> and an actor that opts out of the default events needs `registerEvent<qb::SignalEvent>` as well
> as `registerEvent<qb::KillEvent>`, because `qb::Main::stop()` travels as a signal.
