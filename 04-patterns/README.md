# Tier 04 — the eleven shipped interaction patterns

`qb/core/patterns/` ships eleven headers. Every one is unit- and benchmark-tested, and until this
tier existed **not one of them appeared anywhere in the example corpus** — zero `#include` of
anything under that directory across 113 files, and `qb::ask`, the primitive six of them are built
on, had **zero call sites in 55 programs**. Meanwhile three of the corpus's largest programs
hand-rolled worse versions of three of these patterns.

This tier is that gap, closed. Nine programs, eleven headers, all of it code you do not have to
write.

**Prerequisites: tier 01 and tier 03.** Every pattern except `WorkerPool` and `batcher` is an
awaitable, so you need `spawn` and `co_await` before any of this reads as anything but noise.

The CMake target and the binary are **derived** from each file's path
(`examples/cmake/qbExample.cmake`), so neither is written down anywhere:

| Source | Target and binary | Headers it demonstrates |
|---|---|---|
| `01-pubsub.cpp` | `qb-example-patterns-pubsub` | `pubsub.h` |
| `02-supervisor.cpp` | `qb-example-patterns-supervisor` | `supervisor.h` |
| `03-worker-pool.cpp` | `qb-example-patterns-worker-pool` | `routing.h` |
| `04-scatter-gather.cpp` | `qb-example-patterns-scatter-gather` | `request.h` (incl. `deadline`), `scatter.h` |
| `05-resilience.cpp` | `qb-example-patterns-resilience` | `resilience.h` |
| `06-streaming.cpp` | `qb-example-patterns-streaming` | `streaming.h` |
| `07-saga.cpp` | `qb-example-patterns-saga` | `saga.h` |
| `08-batching-and-idempotency.cpp` | `qb-example-patterns-batching-and-idempotency` | `aggregate.h`, `idempotency.h` |
| `09-discovery.cpp` | `qb-example-patterns-discovery` | `discovery.h` |

```bash
cmake --preset release
cmake --build --preset release --target qb-example-patterns-scatter-gather
./build/presets/release/examples/04-patterns/qb-example-patterns-scatter-gather
```

Each program's header block states what it teaches, what it demonstrates, what it assumes you have
read, and what it prints. `dev/agent/check-example-headers.py` checks the middle two against the
file's code with comments and string literals blanked out, so a promise cannot be its own evidence;
`dev/agent/run-examples.py` checks that every `@expect` line actually reaches a terminal.

## Read them in this order

**`01-pubsub`** — the bus you do not write. `qb::PubSub<Topic>` is a per-core `ServiceActor`:
subscribers join with `subscribe(id())`, publishers call `publish(...)`, and nobody keeps a
registry. Prints the one moment `subscriber_count()` and `tracked_slot_count()` disagree, which is
what a killed subscriber looks like from the bus's side.

**`02-supervisor`** — the two questions supervision actually answers: *what else restarts when this
one dies* (`one_for_one` / `one_for_all` / `rest_for_one`, told apart here by their exact spawn
totals: 4, 6, 5) and *when do we stop restarting* (`max_restarts` inside a window, then
`on_escalate`). Note the cooperative rule: a child that dies without calling `stop()` sends no
`ChildDown` and is not restarted.

**`03-worker-pool`** — `next()` for round-robin, `for_key(k)` for stickiness, and a measurement of
what `remove()` does to your keys. `WorkerPool` is a vector plus a cursor: it sends nothing and
tracks no liveness.

**`04-scatter-gather`** — **start here if you only read one.** `qb::ask` is request/response in one
line, and it replaces a correlation map, a reply handler and a timeout timer every single time. Then
`ask_all`, `ask_all` with `max_in_flight`, `ask_any` and `ask_quorum`, with the elapsed time of each
printed so the trade-offs are numbers rather than adjectives — and finally `qb::deadline`, one
absolute budget threaded through a chain of `ask_by` calls instead of a timeout that resets at
every hop.

**`05-resilience`** — retry with backoff, a circuit breaker that stops sending at all, a token
bucket and a bulkhead. The fifth way to bound a call, `qb::deadline`, belongs to `request.h` and so
lives at the end of `04-scatter-gather`.

**`06-streaming`** — one request, many replies. `ask_stream` + `yield_answer` + `end_stream`, and
the two ways a stream ends badly: a per-chunk timeout, and a bounded buffer that throws rather than
dropping chunks quietly.

**`07-saga`** — undo without a transaction. Compensations run in reverse and only over the steps
that happened; a kill skips them entirely.

**`08-batching-and-idempotency`** — the two halves of "we retried it". `batcher` coalesces writes on
a count or a time trigger; `dedup_map` + `answer_idempotent` make the effect run once per key
however many copies of the request arrive.

**`09-discovery`** — `co_await qb::require<T>` to find actors by type across cores, `co_await
qb::ping` to ask one whether it is still alive, and the sharp edge: `is_actor_alive()` is a
core-local map lookup that will report a live remote actor as gone.

## What each program replaces

The architecture requires a named replacement before anything is retired. The first two have
since BEEN retired, with the whole pre-3.0 holding tree; the third is a half-file and stays:

| Retire | Lines | Replaced by | Why the hand-rolled version was worse |
|---|---|---|---|
| the pre-3.0 `example7_pub_sub.cpp` (RETIRED) | 969 | `01-pubsub` | kept its own subscriber map, could not notice a dead subscriber, and named `qb::PubSub` in a comment without using it |
| the balancer in the pre-3.0 `example10_distributed_computing.cpp` (RETIRED) | 1320 (whole file) | `03-worker-pool` + `04-scatter-gather` | measured assigning half the fleet no work at all; its fan-out half is `ask_all` |
| the supervisor half of `examples/01-actors/05-lifecycle.cpp` | — | `02-supervisor` | polls its workers for status and has no answer to either supervision question. Only that half is superseded: the file keeps its `kill` / `KillEvent` / ordered-teardown lesson |
