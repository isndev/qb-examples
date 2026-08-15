# Example: Graceful Shutdown (`qb-example-services-shutdown-and-drain`)

The fourth tier-5 project, and the one with no networking lesson of its own: everything here is
about **ending**. A server binds a port, takes work, is sent SIGTERM, and then does the four things
that separate a graceful stop from a `kill -9` — in order, each one observable in the output.

Prerequisites: `05-services/01-tcp-chat` (the acceptor / pool / session layout),
`03-coroutines/06-cancellation` (what happens to parked coroutines) and
`01-actors/10-signals-and-shutdown` (a signal is an event).

## What "graceful" means, in order

1. **Stop accepting.** Close the listening socket. A client that connects a millisecond later gets a
   refusal it can retry, which beats a connection accepted and then dropped mid-request.
2. **Drain what you took.** Every request already accepted runs to completion. This is the step
   everybody skips and the one the user notices.
3. **Flush.** A reply that was computed but is still sitting in an output buffer was not delivered.
   `has_pending_write()` is the question; `eos` is the event (see [`02-io/09-graceful-drain`](../../02-io/09-graceful-drain.cpp)).
4. **Exit, saying what happened.** `return 0` from a server that never bound its port is a lie a
   supervisor will believe.

## Layout

| File | Role |
|---|---|
| `main.cpp` | `SessionPool` (the `io_handler`, and the drain), `AcceptActor` (the listener), and `main()` — which reads *two* failure channels before choosing an exit code |
| `session.h` | `WorkSession` — one accepted connection, `use<WorkSession>::tcp::client<SessionPool>` |
| `load.h` / `load.cpp` | `Client` and `Conductor` — the in-process load that gives the drain something to drain |
| `events.h` | `NewConnection`, `ServiceUp`, `Connect`, `WorkRequest`, `WorkDone` — and a comment recording why a `BeginDrain` event was *deleted* |

## Three things it teaches that are easy to get wrong

* **Nobody coordinates the shutdown, and that is the design.** `qb::Main::start()` installs
  SIGINT/SIGTERM handlers that only *record* the signal; each `VirtualCore` then broadcasts a
  `qb::SignalEvent` to the actors it owns. The acceptor, the pool and every client are told
  independently, on their own threads, in order with their other mail. No shutdown coordinator, no
  ordering to get wrong.
* **You must `registerEvent<qb::SignalEvent>(*this)` to act on it.** Dispatch is by *subscription*,
  not by vtable: declaring `on(SignalEvent const &)` without registering hides a name nothing calls,
  and the base handler kills your actor instead. Not hypothetical — the first version of this file
  omitted it on the pool and lost all four in-flight units silently. **`qb::Main::stop()` travels the
  same road**, arriving as a `SignalEvent` and not a `KillEvent`, so an actor built with
  `no_default_events_t` that registers only `KillEvent` can never be stopped.
* **The drain poll is a `qb::ICallback`, not a coroutine, and that is not a style choice.** A
  coroutine may only reach the actor system through its `ctx`, because the actor may be destroyed
  while it is parked — so it cannot read `active_coroutine_count()` at all — and it would itself be
  counted by that number, so the drain would wait for itself. `ICallback` runs on the actor's own
  thread every turn with full access to actor state; the pool registers it when the drain begins and
  unregisters it the moment it ends, so a healthy server never pays for it.

## The exit-code contract

`hasError()` reports **engine** failures: a core that would not start, an actor whose *synchronous*
`onInit()` returned false at startup, an escaped exception. It is necessary and **not sufficient**,
and the gap is measurable in this very corpus: an actor whose **async** `onInit()` returns false once
the engine is running is simply removed, silently, setting no engine flag — which is why both other
servers in this tier stay up with their acceptors gone, holding no port, serving nothing, and exit 0.
`dev/agent/example-run.manifest` records that as `known-bad-stays-up`.

This program does the other thing: a failed bind records an **application** startup failure *and*
asks the engine to stop, and `main()` reads both channels plus the work accounting before choosing a
code.

## Build and run

```bash
cmake --preset release
cmake --build --preset release --target qb-example-services-shutdown-and-drain
./build/presets/release/examples/05-services/04-shutdown-and-drain/qb-example-services-shutdown-and-drain
```

The program raises SIGTERM against its own process, so it demonstrates itself and exits 0. To watch
the exit code change from 0 to 1, take the failure path deliberately:

```bash
./build/presets/release/examples/05-services/04-shutdown-and-drain/qb-example-services-shutdown-and-drain --fail-bind
```
