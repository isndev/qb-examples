# Tier 03 — the 3.0 concurrency model

Coroutines: standalone on `qb-io`, hosted inside an actor, and the primitives that make a
concurrent program readable — awaiting during `onInit`, request/response as one line, combining
awaitables, and cancelling them.

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

`01` and `05` link **qb-io alone**: both are standalone by definition, with neither `qb::Main` nor
`qb::Actor`, and naming `qb-core` in their `DEPENDS` would make that claim untestable.

**The holes 07–11 are the tier's to-do list**, and they are what remains of the corpus's largest
coroutine gap. Measured over all 82 programs, these still have **zero** demonstrators anywhere:
`qb::io::async::coroutine_scope` (and `joining_scope` / `cancelling_scope` / `detaching_scope`) +
`parallel_map`, `channel` + `select`, `generator` / `async_generator` / `async_stream`, the sync
primitives (`semaphore`, `async_mutex`, `barrier`, `shared_task`), and the **qb-io coroutine**
retry family in `qb/io/async/coroutine/retry.h` — `with_retry`, `with_retry_until`, `retry`,
`make_retryable`, `retry_exhausted`.

> Read that last one precisely: there are **three** unrelated things called retry in this tree, and
> only one of them is uncovered. `qb::retry_policy` + `qb::ask_retry` (`qb/core/patterns/resilience.h`)
> are demonstrated by [`04-patterns/05-resilience`](../04-patterns/05-resilience.cpp), and
> `qb::redis::RetryPolicy` + `connect_with_retry` by
> [`06-modules/redis/09-reliability`](../06-modules/redis/09-reliability.cpp). The gap is the
> **generic** `co_await with_retry(f, policy)` over an arbitrary awaitable, which needs no actor
> and no module and belongs in this tier.

Likewise `ScopedCoroContext` — which `06-cancellation` uses throughout — is the **actor's** scope,
not `qb::io::async::coroutine_scope`; the standalone class and its three cleanup policies are
what has no demonstrator.

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
