# Tier 03 — the 3.0 concurrency model

Coroutines: standalone on `qb-io`, hosted inside an actor, and the primitives that make a
concurrent program readable — awaiting during `onInit`, request/response as one line, combining
awaitables, cancelling them, and then the whole structured-concurrency surface: scopes, bounded
fan-out, channels, generators, streams, the sync primitives, retry, and the two adapters for
awaiting something qb does not own.

Prerequisites: tier 01, plus `02-io/01-event-loop` — you need an actor to spawn from and a loop to
be resumed by.

The CMake target and the binary are **derived** from each file's path
(`examples/cmake/qbExample.cmake`), so neither is written down anywhere:

| Source | Target and binary | Links |
|---|---|---|
| `01-first-coroutine.cpp` | `qb-example-coroutines-first-coroutine` | `qb-io` |
| `02-actor-coroutines.cpp` | `qb-example-coroutines-actor-coroutines` | `qb-core` |
| `03-awaiting-oninit.cpp` | `qb-example-coroutines-awaiting-oninit` | `qb-core` |
| `04-ask-request-response.cpp` | `qb-example-coroutines-ask-request-response` | `qb-core` |
| `05-combinators.cpp` | `qb-example-coroutines-combinators` | `qb-io` |
| `06-cancellation.cpp` | `qb-example-coroutines-cancellation` | `qb-core` |
| `07-structured-concurrency.cpp` | `qb-example-coroutines-structured-concurrency` | `qb-io` |
| `08-bounded-fan-out.cpp` | `qb-example-coroutines-bounded-fan-out` | `qb-io` |
| `09-channels.cpp` | `qb-example-coroutines-channels` | `qb-io` |
| `10-generators.cpp` | `qb-example-coroutines-generators` | `qb-io` |
| `11-async-streams.cpp` | `qb-example-coroutines-async-streams` | `qb-io` |
| `12-sync-primitives.cpp` | `qb-example-coroutines-sync-primitives` | `qb-io` |
| `13-retry-and-single-flight.cpp` | `qb-example-coroutines-retry-and-single-flight` | `qb-io` |
| `14-foreign-awaitables.cpp` | `qb-example-coroutines-foreign-awaitables` | `qb-io` |

`01`, `05` and `07`–`14` link **qb-io alone**: all ten are standalone by definition, with neither
`qb::Main` nor `qb::Actor`, and naming `qb-core` in their `DEPENDS` would make that claim
untestable.

## Why 07–14 are grouped the way they are

They were the corpus's largest remaining coverage gap: `coroutine_scope` and its three exit
policies, `parallel` / `parallel_map` / `repeat_while`, `channel` + `select`, `generator` /
`async_generator` / `async_stream` / `interval`, all six sync primitives, `shared_task`, the
**qb-io** `with_retry` family, and `wait_readable` / `wait_for_io` / `async_awaiter` had **zero**
demonstrators across every program in the corpus, despite shipping, being tested and being
documented.

One primitive per file would have produced a catalogue; one file for all of them would have taught
nothing. They are grouped by **the question each answers**, and the grouping is what decides where
a primitive lives:

| Program | The question |
|---|---|
| `07-structured-concurrency` | Who owns a coroutine you spawned, and what happens to it when its owner leaves? |
| `08-bounded-fan-out` | How many pieces of work may be in flight at once — all, K, or one? |
| `09-channels` | How do two coroutines hand values to each other, and how does one say "no more"? |
| `10-generators` | How do you produce a sequence without building it? |
| `11-async-streams` | How do you compose a sequence — including over time? |
| `12-sync-primitives` | Single thread, so no locks needed… right? |
| `13-retry-and-single-flight` | One flaky call and five callers who all want it. |
| `14-foreign-awaitables` | How do you await something the framework does not own? |

Each one is gated on a **measurement**, not on reaching its last line: 07 counts which workers a
`cancel_all()` actually reached (14 of 17), 08 measures peak concurrency (12, then 3, from the same
12 items), 09 counts sends completed with no consumer (3 through a capacity-3 channel, 0 through a
rendezvous), 10 counts source pulls (3 for a synchronous `take(gen, 3)`, 3 for `ag_take`), 11
counts map calls (8, then 3), 12 prints a lost update (balance 10, then 50), 13 counts service
calls (5 from five callers, 1 through a `shared_task`), 14 counts what it bridged.

> **Three unrelated things in this tree are called retry**, and `13` owns exactly one of them.
> `qb::retry_policy` + `qb::ask_retry` (`qb/core/patterns/resilience.h`) belong to
> [`04-patterns/05-resilience`](../04-patterns/05-resilience.cpp), and `qb::redis::RetryPolicy` +
> `connect_with_retry` to [`06-modules/redis/09-reliability`](../06-modules/redis/09-reliability.cpp).
> `13` is the **generic** `co_await with_retry(f, policy)` over an arbitrary awaitable, which needs
> no actor and no module.

Likewise `ScopedCoroContext` — which `06-cancellation` uses throughout — is the **actor's** scope.
`07` is the standalone `qb::io::async::coroutine_scope` and its three cleanup policies; the two are
different objects with different lifetimes, and neither file repeats the other.

## One ownership note the filenames do not tell you

`04-ask-request-response` teaches the **one-to-one exchange** only. The fan-out shapes built on it
(`ask_all` with its bounded `max_in_flight`, `ask_any`, `ask_quorum`) and the shared-budget algebra
(`deadline`, `deadline_in`, `remaining`, `ask_by`) live in
[`04-patterns/04-scatter-gather.cpp`](../04-patterns/04-scatter-gather.cpp), which landed first and is their owner page.
Neither file repeats the other.

```bash
cmake --preset release
cmake --build --preset release --target qb-example-coroutines-combinators
./build/presets/release/examples/03-coroutines/qb-example-coroutines-combinators
```

## Example descriptions

### `01-first-coroutine.cpp`

* **Focus**: a coroutine with no actor and no engine underneath it.
* **QB Features**: `task<T>`, `run_sync` (exact in both directions, unlike `run_for`, which burns
  its whole budget and abandons anything still in flight), `sleep`, `when_all`.
* Two rules stated as rules, because both produced live undefined behaviour in the file it
  replaces: take every coroutine parameter **by value**, and never write `[]() -> task<void> {…}()`.

### `02-actor-coroutines.cpp`

* **Focus**: a coroutine spawned from inside an actor.
* **QB Features**: `spawn` with no trailing `()`, capture by value before the first suspend, talk
  back only through `ctx`, and the `registerEvent<qb::KillEvent>` that makes a derived handler run.

### `03-awaiting-oninit.cpp`

* **Focus**: 3.0's headline behavioural change — `onInit()` is a coroutine, so setup that WAITS is
  written straight down the page.
* **QB Features**: the Activating phase and what it gates (`is_active` vs `is_alive`, the FIFO
  stash, `ActorHandle::get()` returning `nullptr`), `ready_async` instead of a guessed sleep, and
  `qb::VirtualCore::activation_deadline_ns` — lowered to 800 ms so the program can reap a stalled
  init inside its own run.
* Prints two measurements that are **not** what the phase table suggests: `is_active()` is still
  `true` before the first `co_await` (an actor becomes Activating at its first suspension, it is
  not born there), and `Actor::time()` reads **0** during `onInit` at engine start, because the
  core's cached loop clock has not been set yet.

### `04-ask-request-response.cpp`

* **Focus**: request/response between actors as ONE line.
* **QB Features**: `qb::Request<Resp>`, `qb::ask`, `qb::answer`, `resolve_ask` — replacing a
  correlation map, a reply handler and a timeout timer.
* Runs both ways of getting it wrong, so each produces a visible timeout rather than a mystery:
  an asker that forgot `registerEvent<Quote>`, and a responder that built a FRESH reply instead of
  copying the request (losing the correlation id).

### `05-combinators.cpp`

* **Focus**: combining awaitables, and the one property that decides which to reach for — what
  happens to the branches that LOSE.
* **QB Features**: `when_all`, `when_any` + `when_any_result`, `race`, `coro_with_timeout`,
  `with_deadline`, `timeout_error`.
* It counts: ten branches begin, six finish. `when_any` and `with_deadline` **reclaim** their
  losers; `coro_with_timeout` does not — its abandoned task runs to completion and prints, which
  is the whole reason to prefer `with_deadline` when the work has side effects.

### `06-cancellation.cpp`

* **Focus**: why an actor may be killed while its coroutines are parked and nothing leaks.
* **QB Features**: the per-actor scope, `ctx.sleep`, `ctx.cancellation_point()`,
  `ctx.until_cancelled()`, `ctx.cancellable(task)`, `ctx.token()`, `ctx.cancelled()`, and
  `child_token()` for cancelling a subtree without touching the actor.
* An RAII ledger counts handles opened and unwound, so "cancellation is not a kill -9" is a
  printed number. It also runs the A/B that matters: the same 400 ms wait with and without the
  scope token — one wakes on the kill, the bare `qb::io::async::sleep` sleeps its full duration
  and resumes 150 ms after its actor was destroyed.

### `07-structured-concurrency.cpp`

* **Focus**: a scope OWNS the coroutines spawned into it, and its destructor is the one place that
  decides what happens to the ones still running.
* **QB Features**: `coroutine_scope` + `cleanup_policy`, `joining_scope` / `cancelling_scope` /
  `detaching_scope`, `spawn` / `spawn_cancellable`, `join_all` / `join_any` / `join_all_for`,
  `cancel_all` / `cancel_token`, `active_count` / `total_count` / `prune_completed` /
  `rethrow_if_error`, `with_scope`.
* The measurement that matters: **`cancel_all()` is a signal, not a kill.** Two identical workers
  differing only in how they were spawned — the `spawn_cancellable` one is torn down, the plain
  `spawn` one runs to completion. Of the three policies only `cancel_all` does anything a worker
  can feel, and even then only to a worker that was wrapped to receive it. `joining_scope`'s
  destructor joins nothing (it cannot suspend); it warns in a debug build and that is all.
* `join_any` is contrasted with `when_any` on the one axis that separates them: `when_any`
  reclaims the losers, `join_any` merely notifies and leaves them running.

### `08-bounded-fan-out.cpp`

* **Focus**: how many pieces of work are allowed to be in flight at once.
* **QB Features**: `parallel` (all of them, tuple in argument order, `task<void>` →
  `std::monostate`), `parallel_map` with its `max_concurrency`, `repeat_while` (one at a time,
  predicate and token both tested before the factory runs), `capture_result`.
* A gauge records the high-water mark: the same twelve items map twice, with peaks of **12** and
  **3**, from one changed argument. The ceiling is a `semaphore` inside `parallel_map`, so the
  thirteenth caller parks rather than proceeding.

### `09-channels.cpp`

* **Focus**: a queue between two coroutines, whose **capacity is the backpressure policy** and
  whose **close is the whole shutdown protocol**.
* **QB Features**: `channel<T>`, `send`/`recv`, `try_send`/`try_recv`, `recv_for`/`send_for`,
  `close`/`is_closed`, `channel_closed`, `select` + `select_result` over channels of different
  types, `make_channel`, `transform`/`filter`/`collect`, `make_pipeline`.
* Measured with no consumer running: a capacity-3 channel lets **3** sends through and parks the
  fourth; a capacity-0 rendezvous lets **0** through, because a send there is a meeting. A close
  wakes a `select` too, which is how you build a quit signal.

### `10-generators.cpp`

* **Focus**: producing a sequence one value at a time instead of returning a container.
* **QB Features**: `generator<T>` (`co_await` inside one is a compile error — `await_transform` is
  deleted, which is exactly why `async_generator<T>` exists), `range`/`iota`/`take`/`skip`/
  `concat`/`repeat_n`/`from_range`/`collect_to_vector`, `has_next`/`next`, and the `ag_*` family.
* One measurement worth keeping: `take(gen, 3)` and `ag_take(gen, 3)` each pull exactly **3**, and
  the program is gated on the two counts AGREEING. They did not always: `take` used to pull and
  then decide it was past the limit, fetching a fourth value and discarding it — free for `iota`,
  not free for a source whose body consumes a row or a byte. Writing this example is what found it.

### `11-async-streams.cpp`

* **Focus**: composing a sequence, and the only place in this tier where time is first class.
* **QB Features**: `async_stream<T>` with `from_vector`/`from_channel`/`single`/`empty`, the
  transforms `map`/`filter`/`take`/`skip`/`chain`/`buffer`/`throttle`/`debounce`/`backpressure`,
  the terminals `collect`/`count`/`first`/`reduce`/`any`/`all`/`find`/`for_each`/`drain_to`, and
  `interval`/`timer`/`zip`/`merge_streams`/`range_stream`/`repeat_value`.
* A counter inside a `map` reads **0** after the chain is built, **8** after `collect()`, and **3**
  when `.take(3)` is inserted — laziness as a cost difference rather than a description.

### `12-sync-primitives.cpp`

* **Focus**: single-threaded is not the same as synchronisation-free. Every `co_await` is a
  scheduling point, so an invariant spanning one is exactly as broken as one spanning a thread
  switch.
* **QB Features**: `async_mutex`, `semaphore`, `async_rw_lock`, `barrier`, `async_event`,
  `async_latch`, their `scoped_*` guards, the non-suspending `try_*` queries, and
  `with_lock`/`with_semaphore`.
* It produces the defect before fixing it: five read-await-write transfers of +10 leave the balance
  at **10**, not 50, on one thread with no data race and nothing for a thread sanitizer to find.
  The same code under `scoped_lock()` prints **50**.
* One API note worth knowing before you reach for them: `with_lock` and `with_semaphore` take a
  **synchronous** callable, so neither can wrap a critical section that awaits.

### `13-retry-and-single-flight.cpp`

* **Focus**: one flaky operation and several callers who all want it.
* **QB Features**: `with_retry`, `with_retry_until` (retries on a RESULT, not an exception),
  `retry`, `make_retryable`, `retry_policy` + `backoff_strategy`, `retry_exhausted` with
  `attempts()` / `rethrow_last()`, the three ready-made policies, and `shared_task<int>` +
  `make_shared_task`.
* Five independent callers make **5** calls; the same five sharing one `shared_task` handle make
  **1**, and a late joiner gets the cached result without re-running anything. A non-retryable
  error is rethrown as itself after exactly one attempt, so the caller sees the real cause.

### `14-foreign-awaitables.cpp`

* **Focus**: awaiting something the framework does not own.
* **QB Features**: `wait_readable` / `wait_writable` / `wait_for_io` over a raw handle, and
  `async_awaiter<T>` as the general bridge from any callback-shaped API.
* A heartbeat coroutine runs alongside the parked reader and gets its ticks in, which is the
  difference between waiting and blocking. The socket is a `qb::io::udp::socket` on an **ephemeral**
  loopback port rather than a `pipe()`, because `native_handle()` is an `int` on POSIX and a
  `SOCKET` on Windows and each `wait_*` ships an overload for both.
