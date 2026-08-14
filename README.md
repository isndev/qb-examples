# QB Framework — the example corpus

The corpus is organised **by level** — the order in which a person should read it — rather than by
which library a program links. The filesystem sorts, so the reading order is visible without
opening anything; the topic is in the filename, so the tree is still greppable for someone who
arrived with a task rather than a curriculum.

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
| [`01-actors/`](./01-actors/README.md) | The actor model: actors, events, cores, lifetime, state machines, service actors, actor trees, signals and the hot path. Eleven programs, no gaps. | none |
| [`02-io/`](./02-io/README.md) | `qb-io` **standalone** — an event loop, files, TCP, UDP and a hand-written wire protocol, with no actor and no `qb::Main`. | none, deliberately |
| [`03-coroutines/`](./03-coroutines/README.md) | The 3.0 concurrency model: `task<T>`, `run_sync`, coroutines spawned from inside an actor, an `onInit` that really awaits, `qb::ask`, the combinators and cancellation. | 01, 02/01 |
| [`04-patterns/`](./04-patterns/README.md) | The eleven shipped `qb/core/patterns/` headers — pub/sub, supervisor, worker pool, `qb::ask` + scatter/gather, resilience, streaming, saga, batching + idempotency, discovery. Nine programs; before them the whole directory had **zero** demonstrators and `qb::ask` had zero call sites. | 01, 03 |
| [`05-services/`](./05-services/) | Actors **plus** `qb-io`: the architecture of a real server — a TCP chat, a pub/sub broker, a file pipeline. | 01–04 |
| [`06-modules/`](./06-modules/README.md) | The qbm modules: [`http/`](./06-modules/http/README.md), [`ws/`](./06-modules/ws/README.md), [`pgsql/`](./06-modules/pgsql/README.md), [`redis/`](./06-modules/redis/README.md). | 01, 03 (+ 02 for the protocol tiers) |
| [`07-applications/`](./07-applications/) | Full-stack projects: [taskmanager](./07-applications/01-taskmanager/README.md), [auction-house](./07-applications/02-auction-house/README.md). | everything |

**The gaps in the numbering are deliberate.** A tier's holes are its to-do list, and they are named
in that tier's `CMakeLists.txt`. Numbering densely now would renumber every later file — and every
citation of it — the day one is written.

## The pre-3.0 holding directories

`core/`, `core_io/` and `qbm/` are not tiers. Each holds only the programs the restructure
**retires or merges** rather than moves, and a retirement lands *with* its replacement — never
before it, or the corpus promises something no file delivers. Each directory's `CMakeLists.txt`
names which replacement it is waiting for; the last one to arrive takes the directory with it.

| Still there | Waiting for |
|---|---|
| `core/example6_shared_queue.cpp` | `01-actors/03-event-payloads` — **landed**; the retirement is unblocked and is its own step |
| `core/example7_pub_sub.cpp` | `04-patterns/01-pubsub` — **landed**; the retirement is unblocked and is its own step |
| `core/example9_trading_system.cpp` | `07-applications/03-market-data-hub` |
| `core/example10_distributed_computing.cpp` | `04-patterns/03-worker-pool` + `04-patterns/04-scatter-gather` — **both landed**; what remains before it can go is a home for its monitoring half (`02-io/11-logging-and-metrics`) |
| `core_io/file_monitor/` | `02-io/08-timeouts-and-watchers` + `01-actors/03-event-payloads` (**landed**) + `05-services/03-file-pipeline` (**landed**) |
| `qbm/http/06_async_handlers.cpp` | `06-modules/http/04-middleware` + `06-modules/http/09-coroutine-handlers` |
| `qbm/redis/example2_hash_operations.cpp`, `example3_list_operations.cpp` | merged into `06-modules/redis/02-data-types` |
| `qbm/redis/example8_complex_actor_system.cpp` | `06-modules/redis/07-scripting` + `10-cache-actor` |

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
