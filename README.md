# QB Framework — the example corpus

The corpus is organised **by level** — the order in which a person should read it — rather than by
which library a program links. The filesystem sorts, so the reading order is visible without
opening anything; the topic is in the filename, so the tree is still greppable for someone who
arrived with a task rather than a curriculum.


> **Which example demonstrates what?** [Capability index](CAPABILITY-INDEX.md) is generated from every program's
> `@demonstrates` block and gated byte-exact, so it cannot drift from the corpus it describes.

An example's CMake **target** and **binary** are *derived* from its path
(`examples/cmake/qbExample.cmake`) and never written by hand:

    02-io/03-tcp.cpp                     ->  qb-example-io-tcp
    06-modules/http/02-routing.cpp       ->  qb-example-modules-http-routing
    05-services/01-tcp-chat/ + ROLE server ->  qb-example-services-tcp-chat-server

Every program also carries a header block — `@teaches`, `@demonstrates`, `@prerequisites`,
`@expect` — and `dev/agent/check-example-headers.py` asserts that every `@demonstrates` name really
occurs in that file's **code**. A name that is not true of the file under it is the corpus's
recurring defect, and that is the guard against it.

**An `@expect` line is an assertion, not a label.** Wherever a program can measure the thing it
teaches, it prints a WHOLE sentence chosen by that measurement rather than splicing a value into
one — so `dev/agent/run-examples.py` failing to find the line means the behaviour changed, not
that the wording did.

## The tiers

| Tier | What it teaches | Prerequisite |
|---|---|---|
| [`01-actors/`](./01-actors/README.md) | The actor model: actors, events, cores, lifetime, state machines, service actors, actor trees, signals, the hot path, and the boundary with threads the engine does not own (`qb::lockfree` spsc/mpsc + `SpinLock`). Twelve programs, no gaps. | none |
| [`02-io/`](./02-io/README.md) | `qb-io` **standalone** — an event loop, files, TCP, UDP, a hand-written wire protocol, the shipped framing toolbox, TLS, the timer/watcher contracts, the drain vocabulary, crypto and compression, the logger, and QUIC. Twelve programs, no gaps. | none, deliberately |
| [`03-coroutines/`](./03-coroutines/README.md) | The 3.0 concurrency model: `task<T>`, `run_sync`, coroutines spawned from inside an actor, an `onInit` that really awaits, `qb::ask`, the combinators and cancellation — then the whole structured-concurrency surface in `07`–`14`: scopes and their exit policies, bounded fan-out, channels + `select`, generators, async streams, the six sync primitives, retry + `shared_task`, and awaiting a raw handle or a callback. Fourteen programs. | 01, 02/01 |
| [`04-patterns/`](./04-patterns/README.md) | The eleven shipped `qb/core/patterns/` headers — pub/sub, supervisor, worker pool, `qb::ask` + scatter/gather, resilience, streaming, saga, batching + idempotency, discovery. Nine programs; before them the whole directory had **zero** demonstrators and `qb::ask` had zero call sites. | 01, 03 |
| [`05-services/`](./05-services/) | Actors **plus** `qb-io`: the architecture of a real server — a TCP chat, a pub/sub broker, a file pipeline, and a shutdown that stops accepting, drains, flushes and exits with an honest code. Four projects, no gaps. | 01–04 |
| [`06-modules/`](./06-modules/README.md) | The qbm modules: [`http/`](./06-modules/http/README.md) (15), [`ws/`](./06-modules/ws/README.md) (4), [`pgsql/`](./06-modules/pgsql/README.md) (10), [`redis/`](./06-modules/redis/README.md) (14). Each group covers both halves of its client's API — the `co_await` one and the callback one — and, for pgsql and redis, the command families that ship with a readme page. | 01, 03 (+ 02 for the protocol tiers) |
| [`07-applications/`](./07-applications/) | Full-stack projects: [taskmanager](./07-applications/01-taskmanager/README.md), [auction-house](./07-applications/02-auction-house/README.md), [market-data-hub](./07-applications/03-market-data-hub/README.md) — the one with no HTTP and no SQL. | everything |

**The gaps in the numbering are deliberate.** A tier's holes are its to-do list, and they are named
in that tier's `CMakeLists.txt`. Numbering densely now would renumber every later file — and every
citation of it — the day one is written.

## The pre-3.0 holding directories, and what became of them

Every tier is converted, and the five pre-3.0 holding directories (`core/`, `core_io/`, `qbm/`,
`coroutine/`, `all/`) have been **retired**. Every program in this tree now derives its CMake
target and its binary name from its path; there is no second naming convention left anywhere.

A retirement only lands *with* its replacement — never before it, or the corpus promises
something no file delivers. Each was checked one program at a time against the replacement's
CODE, not against the claim in its `CMakeLists.txt`:

| Retired | Replaced by |
|---|---|
| `core/example6_shared_queue.cpp` | `01-actors/03-event-payloads` — the same foreign-thread bridge, with a lock-free spsc ring in place of a mutex-guarded queue |
| `core/example7_pub_sub.cpp` | `04-patterns/01-pubsub` for the shipped `qb::PubSub<Topic>` bus, and `05-services/02-pubsub-broker` for runtime *topic-keyed* routing, which the bus does not do |
| `core/example9_trading_system.cpp` | `07-applications/03-market-data-hub` |
| `core/example10_distributed_computing.cpp` | `04-patterns/03-worker-pool` + `04-patterns/04-scatter-gather` + `02-io/11-logging-and-metrics` |
| `core_io/file_monitor/` | `02-io/08-timeouts-and-watchers` + `01-actors/03-event-payloads` + `05-services/03-file-pipeline` |
| `qbm/http/06_async_handlers.cpp` | `06-modules/http/04-middleware` + `06-modules/http/09-coroutine-handlers` |
| `qbm/redis/example2_hash_operations.cpp`, `example3_list_operations.cpp` | merged into `06-modules/redis/02-data-types` |
| `qbm/redis/example8_complex_actor_system.cpp` | `06-modules/redis/07-scripting` + `10-cache-actor` |

**Three lessons had no home, and were given one rather than used as a reason to keep a
superseded file alive.** `HKEYS`/`HVALS`/`LINDEX`/`LSET`/`BLPOP` survived only in prose
describing the two merged programs, so they went into `06-modules/redis/02-data-types`, which
*is* the merge. A plain cursor-based `XREAD` — a stream read with no consumer group — existed
nowhere, so it went into `06-modules/redis/06-streams`. And "relocatable is not owned" (boxing a
`shared_ptr` into an event settles whether the EVENT can be memcpy'd and says nothing about who
may write through the POINTEE) went into `01-actors/03-event-payloads`, beside the rule it is
the second half of.

## Building and running

This directory is its own repository (`isndev/qb-examples`) and **cannot be configured standalone**:
its `CMakeLists.txt` calls `qb_status_message`, `qb_add_executable` and
`qb_stage_example_resources`, which only qb defines, and it returns without adding a target when
`QB_BUILD_EXAMPLES` is undefined. Build it from the superproject:

```bash
cmake --preset release
cmake --build --preset release --target qb-example-modules-http-routing
./build/presets/release/examples/06-modules/http/qb-example-modules-http-routing
```

Examples that read assets (`resources/...`) have them staged next to the binary, so they run from
any working directory. Each tier and module directory has its own README with the details.
