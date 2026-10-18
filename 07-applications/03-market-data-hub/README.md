# 03 — market-data-hub

A complete qb application with **no HTTP and no SQL**.

The other two programs in tier 07 are HTTP + PostgreSQL + Redis services, and so were the two
largest programs in the pre-3.0 corpus. Read only those and you have been shown, without anyone
saying it, that qb is a web framework. It is not — and this is the shape that argument takes.

```
a real std::thread ─spsc ring─▶ IngestActor ─send<>─▶ 3 × AggregatorActor ─push─▶ PublisherActor
                                                                                        │
                                                                            binary frames over TCP
                                                                                        ▼
                                                                                 SubscriberActor
```

| Stage | What it demonstrates |
|---|---|
| feed thread | `qb::lockfree::spsc::ringbuffer` — the sanctioned bridge from a thread that knows nothing about actors |
| `IngestActor` | `qb::ICallback` drain per loop turn, `qb::WorkerPool::for_key` sticky routing, `send<>` on the hot path |
| `AggregatorActor` | per-symbol state with no locking, latest price guarded by sequence despite unordered ticks, `qb::batcher` coalescing |
| `PublisherActor` | `qb::io::use<T>::tcp::server<Session>` inside an actor, a 36-byte binary record, feed-to-handler-entry latency |
| `SubscriberActor` | the other end of the wire, rejecting malformed records and recording each symbol for the final producer comparison |

## Layout

```
include/market_data/model.h      the domain, and the cross-core payload rule it obeys
include/market_data/protocol.h   the wire: size_as_header framing + big-endian fields
src/actors.h                     the four actors
src/main.cpp                     engine wiring, the feed thread, the report  (carries the header block)
tests/feed-stop.cpp              producer shutdown, unordered latest price, wire and oracle controls
scripts/check_startup_failure.py  publisher bind, subscriber connect and early-disconnect controls
```

## The number at the end

It covers the feed thread's timestamp through entry to the Publisher's batch handler. It includes
the ring, actor queues and batch wait; it excludes conversion, encoding, socket writes and subscriber
reception. It is printed as a **distribution** (min / p50 / p90 / p99 / max), not a headline.
Compare shapes between two builds on one machine, never medians between machines. Completion is a
separate oracle: the producer independently counts every tick and records per-symbol totals. The
subscriber checks record shape and monotonic sequence; after shutdown, the final audit compares
counts and decoded per-symbol price/VWAP/volume with the producer. Intermediate prices and VWAPs
are not compared to a separate reference.

## Startup and stream controls

* Before qb's 2026-08-16 connector fix (`e4fca78a`), this example observed a TCP connection
  established while a direct `co_await` in `onInit()` did not resume. That is historical: current
  qb tests a direct connect as the first suspension of `onInit()` and checks both resumption and an open socket
  (`InitIoAwaiters.ConnectInInitResumes`). This example keeps the connection in `spawn()` so
  `onInit()` returns before the socket is ready; `SubscriberSocketReady` installs the socket and
  starts the subscriber before the feed begins. `spawn()` is an ordering choice here, not a
  requirement for `tcp::connect` inside `onInit()`.
* Publishing before a subscriber has attached sends those frames nowhere, so the feed thread waits
  for one. Without that gate the first ~10 000 quotes were lost and the final count measured
  startup timing rather than the pipeline.
* The feed starts only after both the publisher has accepted a socket and the subscriber has
  installed its own end of that socket. A failed publisher bind, failed subscriber connect, or
  disconnect before the end-of-stream marker returns a failed verdict instead of leaving the
  feed parked. If the engine stops with a full ring, the stop flag lets the producer join; the
  flag is read only on the waiting paths, not on a successful tick enqueue.
* Each shard's end marker carries its sent-tick count. The aggregator flushes only after that
  many ticks arrive, even if unordered `send<>` delivery puts the marker ahead of an older tick.
  A missing tick fails within `kShardDrainTimeout` (20 seconds, `include/market_data/model.h`)
  and stops the engine instead of hanging the standalone binary before its final oracle.

## Build and run

```sh
cmake --preset release
cmake --build --preset release --target qb-example-applications-market-data-hub
./build/presets/release/examples/07-applications/03-market-data-hub/qb-example-applications-market-data-hub
python3 examples/07-applications/03-market-data-hub/scripts/check_startup_failure.py \
  ./build/presets/release/examples/07-applications/03-market-data-hub/qb-example-applications-market-data-hub
ctest --preset release -R qb-examples-test-system-market-
```
