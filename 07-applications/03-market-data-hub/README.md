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
| `AggregatorActor` | per-symbol state with no locking (sticky routing is what makes that safe), `qb::batcher` coalescing |
| `PublisherActor` | `qb::io::use<T>::tcp::server<Session>` inside an actor, a binary wire format, the latency measurement |
| `SubscriberActor` | the other end of the wire, in the same process, so the format is proven rather than asserted |

## Layout

```
include/market_data/model.h      the domain, and the cross-core payload rule it obeys
include/market_data/protocol.h   the wire: size_as_header framing + big-endian fields
src/actors.h                     the four actors
src/main.cpp                     engine wiring, the feed thread, the report  (carries the header block)
```

## The number at the end

It is printed as a **distribution** (min / p50 / p90 / p99 / max), not a headline. A single figure
would be a claim about hardware this program has never run on; and the batch window (4 ms) plus the
ring backlog dominate the tail by construction, so a p99 that looks bad is usually a batch that was
waiting on purpose. Compare the shape between two builds on one machine — never medians between
machines.

## Two measurements recorded in the source

* `co_await qb::io::async::tcp::connect<Transport>(uri, timeout)` written directly in an actor's
  `onInit()` suspends and **never resumes**, although the TCP connection really is established.
  The same expression inside `spawn()` resumes normally — see `src/actors.h`.
* Publishing before a subscriber has attached sends those frames nowhere, so the feed thread waits
  for one. Without that gate the first ~10 000 quotes were lost and the final count measured
  startup timing rather than the pipeline.

## Build and run

```sh
cmake --preset release
cmake --build --preset release --target qb-example-applications-market-data-hub
./build/presets/release/examples/07-applications/03-market-data-hub/qb-example-applications-market-data-hub
```
