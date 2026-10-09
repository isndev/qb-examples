# Changelog

All notable changes to qb-examples are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the corpus versions in
lockstep with the qb train (see the release policy in the qb-dev superproject's AGENTS.md):
an entry belongs here only when it changes what a USER of these examples sees — a program's
behaviour, its exit contract, a lesson's content. Build scripts, CI and the runner's own
plumbing do not qualify.

## [Unreleased]

### Added

- **`01-actors/04-cores-and-placement` reads its cores' counters (Huly QB-162).** Each worker reports what its core
  has received and published to other cores, and the dispatcher how many events core 0 published, with
  `getCoreStats()`: its pushes to the worker sharing core 0 never leave that core, so they show as received there and
  never as sent. One new line, `DispatcherActor: core 0 published ...`.
- **`01-actors/13-death-watch` (Huly QB-51).** One watcher on core 0 learns how five actors ended, one after the
  other and with no clock: killed on its core and on core 1, after their destructors; an `onInit()` that failed while
  it was being watched (`init_failed`); an id nobody holds any more (`unknown`); and one taken back with `unwatch()`
  while the answer about it was already on its way, which never arrives.
- **`03-coroutines/15-offloading-blocking-work` (Huly QB-69).** The same 300 ms blocking call is made beside a 5 ms
  heartbeat, inline and then through `co_await qb::io::async::offload(...)`, and the heartbeat's worst gap is printed
  both times: the whole call, then one tick. An exception thrown on the pool is caught on the loop; then an actor is
  killed while its `ctx.offload` call still runs, the wait ends at once, and the call's late result is discarded on
  its core and counted, handed to no one.

### Changed

- **`04-patterns/02-supervisor` shows `qb::supervision::watch` and reads exact totals (Huly QB-51).** A fifth phase
  runs the watch mode: slot 1 first `stop()`s, a `ChildDown` and a `DownEvent` for one death and one restart, then
  its replacement is `kill()`ed, which only the watch sees. Every phase that ends on a count now reads it after 20
  quiet passes and prints it in full, `= 4 spawns` and so on, so a restart too many would show where the program used
  to stop at the count it expected. The header's "it is cooperative" section says when it is not.

### Fixed

- **Market Data Hub exits after either end of its wire fails (Huly QB-808).** A failed
  publisher bind no longer launches a feed thread that waits forever. A subscriber
  connect failure or disconnect before the end marker stops the engine, releases the
  feed's subscriber/full-ring waits, and returns a failed verdict. A disconnect after
  the marker remains a normal completion. The 20,000-tick pipeline is unchanged.
- **The HTTP static-files example confines `/browse` to its configured static root (Huly QB-792).**
  Raw and percent-encoded parent paths, plus outward symlinks, now receive 403; ordinary child
  directories and inward symlinks remain browsable.
- **The HTTP upload example keeps same-name uploads distinct and checks stored bytes (Huly QB-791).**
  A per-server sequence prevents rapid uploads from truncating one another; failed writes,
  flushes, closes, or an incomplete stored size no longer receive 201 or metadata.
- **The HTTP Book PATCH and file metadata update commit only a fully valid change (Huly QB-788).**
  If a later field has the wrong JSON type, the 400 response leaves the existing object unchanged.
- **Redis Pub/Sub lesson describes disconnect and deferred resumption (Huly QB-252).**
  Disconnect closes the receive channel before shutdown kills the actor, but the parked
  coroutine resumes on a later scheduler pass and may outlive the actor. The lesson no
  longer claims that only the consumer destructor wakes it.
- **The WebSocket chat server stops cleanly with connected users (Huly QB-830).** A session
  destructor broadcast a departure while the server's session map was being destroyed,
  recursively destroying sessions until the process crashed. Ordinary disconnection now
  announces one departure while the server is alive; shutdown with 0, 1 or 2 announced
  clients now exits cleanly. The example also uses qb's synchronized console output so its
  main thread and HTTP worker do not race while printing startup and session lines.
  A live-client shutdown check runs against the built example.
- **Auction House records only an accepted bid under overlapping requests (Huly QB-801).**
  The old `BEGIN`/insert/update/commit sequence spanned several coroutine suspensions on
  one worker's PostgreSQL connection; a rejected bid could be committed by a concurrent
  handler. One guarded SQL statement now updates the lot and inserts the bid together.
  The live application check covers overlapping sessions on one and separate workers,
  one stored row, rollback on insert failure, and a valid follow-up bid.
- **Auction House refuses bids on cancelled or not-yet-open lots (Huly QB-914).** The
  guarded update now requires `status = 'active'` and `start_time <= NOW()` alongside
  the existing end-time and price checks; neither a lot's price nor bid history changes.
- **Auction House reports the stored price after rounding (Huly QB-915).** The HTTP
  response and WebSocket event use the price returned by PostgreSQL, so an offer
  such as 110.009 reports 110.01, matching the committed row.
- **`02-io/05-custom-protocol`'s disconnect handlers run (Huly QB-252).** Both were declared
  `on(qb::io::async::event::disconnected &)`, a non-const lvalue reference, which never binds the rvalue the event
  is dispatched as: neither was ever called, so the client could not leave its loop when the server went. They take
  `&&` now.
- **`06-modules/pgsql/07-listen-notify` teaches the fixed consumer (Huly QB-252, QB-253).** Its section 5 measured
  the defect -- a reconnected `notify_co_consumer`'s `receive()` answering `nullopt` for ever, remedied with a second
  consumer -- and its comments said `disconnect()` aborts a debug build when called from a coroutine. It now
  measures that the same consumer's `receive()` hands over the notification the re-LISTEN let through, says
  `disconnect()` is safe there, and its `[reuse]` line changed with it; the drop handler is documented as the
  backpressure signal it now only is.
- **The taskmanager and auction-house comments describe the subscriber's shutdown as it now happens (Huly
  QB-252).** They said `shutdown()`'s `disconnect()` does NOT end the receive loop -- which was the defect: the
  consumer's disconnect handler never ran. It ends it now. The rule they keep is the loop's: its tail touches nothing
  of the actor, because `close()` only schedules the resume and the actor, killed in the same handler, is reaped
  first.
- **`02-io/08-timeouts-and-watchers` no longer says `ev_stat` is never inotify (Huly QB-204).** On Linux
  inotify wakes it for a path on a filesystem libev knows to be local, and it polls everywhere else; the
  header block, the interval comment and the closing line say so. What the program teaches is unchanged:
  the event is two `stat`s, so a directory event says THAT something changed, never WHAT.

## [3.2.1] - 2026-09-24

Lockstep release with the qb 3.2.1 train; no change in this repository (the version says compatible, this section says unchanged).

## [3.2.0] - 2026-09-21

### Fixed

- **Two programs whose ending was decided by one core's timeline — the corpus runner's
  two signals, a hang and a dead path, each seen once on the arm64 CI runner.**
  `02-io/03-tcp` gated its shutdown command on a flag the SERVER thread clears when it sees
  the client's socket close — the client object's destructor, microseconds before
  `join()` returns — so the server's 10 ms tick could win and no shutdown was ever sent: a
  hang after the full script had printed, won ~999 times in 1 000 on x86 and lost 2 of 16
  on the arm64 VM; `main` decides on the server's own state now. `04-patterns/01-pubsub`
  stopped the engine on the survivor's second wave alone while the polite desk's exit ran on
  the other core with nothing ordering the two, so a run could print `=== pub/sub complete
  ===`, exit 0, and never print `[bus@1] a POLITE subscriber reclaims its slot at once`;
  each feed now reports to the reporter once its bus has judged the departed desk, and the
  run ends only when both buses have spoken AND the survivor's wave has landed (40/40 quiet,
  40/40 under twelve busy neighbours on WSL2). The lessons are untouched; what changed is
  that both programs now end for a reason every core agrees on.

## [3.1.0] - 2026-08-30

### Changed

- **This repository gets its own CI (Huly QB-7).** Until then 99 programs protected no push to the
  repository that owns them. A lane checks out the same-named qb and qbm branches, drives the
  superproject's superbuild, and holds floors that refuse vacuity: at least 90 translation units
  compiled, a roster of at least 99 built rows, every built binary present, one self-contained
  program run — with clang pinned like every other lane, and the roster floor set to what
  `ubuntu-latest` can build.
- **The two tier-7 post-SIGTERM `@expect` tails lose their platform tag**
  (`07-applications/01-taskmanager`, `07-applications/02-auction-house`): qb 3.1.0's console-control
  bridge delivers CTRL_BREAK as the SIGTERM their teardown already handles, so the lines the tag
  excluded on Windows are assertable there. The platform-tagged count returns to 2, both honest
  bounds (SIGHUP's existence, AFD's send buffer), neither a framework gap.

## [3.0.1] - 2026-08-29

### Fixed

- **A failed bind no longer reports success — three programs, one defect class.**
  `02-io/05-custom-protocol` ran its server and client through `void` helpers whose failure
  `return;` never reached `main`, which returned 0 unconditionally: a server that could not
  bind printed the error and exited clean. Both helpers now return `main`'s exit code — a
  held port is exit 1, like the other fifteen servers in this corpus. The two
  `05-services` servers (`01-tcp-chat`, `02-pubsub-broker`) had the deeper form: their
  acceptor's async `onInit` correctly `co_return false`s and the framework aborts every
  core at the init barrier — but `main` never consulted `engine.hasError()`, printed
  `Engine is running` over cores that had already exited, and sat waiting on stdin. Both
  mains now gate on `hasError()` — which `start(true)` makes answerable the moment it
  returns — and exit 1 with a message. Teaching programs were teaching exactly the failure
  mode a supervisor cannot see.

## [3.0.0] - 2026-08-20

The corpus as released with qb 3.0.0: 99 programs across 7 tiers, every one carrying a
verified header contract (`@expect`/`@demonstrates`) and run — not just built — by the
superproject's example runner on macOS, Linux and Windows.

### The pre-3.0 holding directories, and what became of them

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

[Unreleased]: https://github.com/isndev/qb-examples/compare/v3.2.1...HEAD
[3.2.1]: https://github.com/isndev/qb-examples/compare/v3.2.0...v3.2.1
[3.2.0]: https://github.com/isndev/qb-examples/compare/v3.1.0...v3.2.0
[3.1.0]: https://github.com/isndev/qb-examples/compare/v3.0.1...v3.1.0
[3.0.1]: https://github.com/isndev/qb-examples/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/isndev/qb-examples/releases/tag/v3.0.0
