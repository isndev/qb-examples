# Changelog

All notable changes to qb-examples are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the corpus versions in
lockstep with the qb train (see the release policy in the qb-dev superproject's AGENTS.md):
an entry belongs here only when it changes what a USER of these examples sees — a program's
behaviour, its exit contract, a lesson's content. Build scripts, CI and the runner's own
plumbing do not qualify.

## [Unreleased]

### Added

- **PostgreSQL COPY and CancelRequest have a live lesson (Huly QB-121).**
  `11-copy-and-cancel` streams CSV in and out, observes session metadata and negotiated SCRAM,
  cancels a server-confirmed active query with `cancel_async()`, and verifies the connection survives.
- **Redis single-node lock has a live lesson (Huly QB-139).** `15-locks` uses a CSPRNG owner token,
  SET NX PX, token-checked Lua renewal and release, and checks expiry against a stale owner.
  It needs SSL for random bytes; the existing atomic rate limiter remains in lesson 08.

- **Auction House accepts a separate Redis endpoint (Huly QB-916).** `REDIS_HOST` and
  `REDIS_PORT` select the connection at startup; the default remains `localhost:6379`.
  Malformed endpoints fail before database initialization, allowing the normal application
  binary to be validated against disposable services.
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

- **Auction House builds accepted-bid replies with fewer temporary allocations (Huly QB-916).**
  Success and replay retain the same six JSON fields, numeric types, status and content type.
  The reply owns the request UUID and serializes the object once; database reconciliation,
  cache invalidation and notification order are unchanged.

- **The launch guide states which resource paths depend on the working directory (Huly QB-857).**
  Build staging places `resources/` beside each binary, while the corpus runner also launches from
  that directory. Examples that deliberately pass relative paths therefore need that working
  directory; examples using qb's resource resolver remain relocatable.
- **`04-patterns/02-supervisor` shows `qb::supervision::watch` and reads exact totals (Huly QB-51).** A fifth phase
  runs the watch mode: slot 1 first `stop()`s, a `ChildDown` and a `DownEvent` for one death and one restart, then
  its replacement is `kill()`ed, which only the watch sees. Every phase that ends on a count now reads it after 20
  quiet passes and prints it in full, `= 4 spawns` and so on, so a restart too many would show where the program used
  to stop at the count it expected. The header's "it is cooperative" section says when it is not.

### Fixed

- **Browser chat keeps messages visible across equal usernames and reconnects (Huly QB-864).**
  It renders accepted server echoes once, uses a per-message ID for own styling, and cannot
  replace an already open or connecting socket from a stale visibility timer.
- **Coroutine WebSocket session selects only an offered subprotocol (Huly QB-865).**
  Exact comma-delimited token matching prevents a near-match from being echoed as accepted.
- **Coroutine WebSocket client proves its control, buffer and close claims (Huly QB-866).**
  The fixture observes Pong before judging a bounded burst, reads the peer's Close frame,
  and distinguishes a raw transport drop; its one-awaiter check uses a rendezvous.
- **MPSC lesson drains the last published job (Huly QB-718).** Its bounded callback
  reads producer completion before checking both queues empty, so a final enqueue
  cannot arrive between the empty check and retirement.
- **Event-loop lesson handles native signals without logging inside the handler
  (Huly QB-719).** A lock-free atomic carries the signal to the main loop; output
  runs after the handler returns.
- **TLS and graceful-drain lessons release their async clients before the local
  listener ends (Huly QB-723).** Normal and early returns clean up the client owners
  while the event loop they registered with still exists.
- **Redis Pub/Sub waits for every confirmed subscription (Huly QB-825).** The lesson
  publishes only after both subscribers have acknowledged both channels, and ignores
  duplicate or mismatched acknowledgements.
- **Broker publication keeps owned message storage (Huly QB-782).** The unused constructor
  that could leave topic and content views pointing into a moved message is removed.
- **Cancellation's long-running example has defined arithmetic on Windows (Huly QB-767).**
  Its deliberately unbounded counter uses unsigned 64-bit wraparound instead of signed overflow.
- **File pipeline handles partial reads and isolates each run (Huly QB-754, QB-755).**
  A short file read continues until the expected byte count or reports EOF; each run reserves
  and removes its own temporary directory, including after an error.
- **TaskManager keeps recent changes through overlapping reloads and cache refills
  (Huly QB-795, QB-794).** Notifications during a pending GET trigger a follow-up GET;
  generation-checked Redis refill cannot restore a stale task after invalidation.
- **Auction House keeps bids and prices consistent across late replies (Huly QB-810,
  QB-812, QB-813).** Zero-bid lots show their starting price; a pending bid retains its
  original lot and bidder when the drawer changes; HTTP, WebSocket and late history replies
  share bid IDs, deduplicate counts, and cannot lower the displayed price.
- **TaskManager starts all workers on a fresh database and preserves partial update intent (Huly QB-1030, QB-796, QB-797).**
  Schema creation is serialized and checked before each worker becomes ready. An omitted
  description keeps its old value, while an explicit empty string clears it. The route smoke
  test now fails on wrong responses, missing IDs, cache drift or transport errors.
- **HTTP lessons honor their route and redirect contracts (Huly QB-793, QB-821).**
  A bodyless `GET /api/users/:id` no longer receives the POST body schema. The HTTP redirect
  targets the HTTPS listener on port 8443, preserves path and query, and accepts only the
  loopback Host values this local demonstration can serve.
- **The upload form survives repeated attempts (Huly QB-822).** Its progress element remains
  in the document after status changes, concurrent submissions are ignored, and a failed
  network request leaves the control usable for retry.
- **The broker client folds command bytes inside `std::toupper`'s valid domain (Huly QB-856).**
  A high-bit byte in an unknown command can no longer enter the C character API as a negative
  signed `char`; ASCII command matching is unchanged.
- **Chat and broker wait for incomplete frames and reject invalid ones (Huly QB-472, QB-473, QB-752).**
  Their parsers wait for the complete body, cap payloads at 64 KiB and the connection's I/O limit
  before allocation, and close on bad magic, version or length. Fragmented and coalesced frames
  remain valid; the guides show the same contract.
- **Chat keeps one accepted username per session (Huly QB-753).** A second authentication is
  rejected before either name index changes, so disconnect releases the original name.
- **The interactive custom-protocol client publishes only on its I/O owner (Huly QB-720).**
  A bounded queue transfers stdin lines to that thread; `quit` waits for earlier output and
  replies, or a six-second deadline, before exit. EOF remains an ordered terminal input.
- **Interactive examples can stop with stdin open (Huly QB-721, QB-750, QB-1029, QB-1031).**
  A bounded reader outside actor cores transfers complete lines and EOF; shutdown wakes
  an unfinished read before joining its worker. Chat, broker, WebSocket and custom-protocol
  clients no longer hold an actor core in `getline`. Chat, broker and custom-protocol servers
  respond to Enter or external stop while a redirected stdin pipe stays open; server EOF
  leaves them serving, while an input error stops them explicitly.
- **The WebSocket client honors its startup URL and serializes console output (Huly QB-1032, QB-1033).**
  After the network actor publishes its ID, the command actor sends one connect event through
  the same path as typed `/connect`; the URL must fit its 256-byte event field. The focused
  stdin regression waits for a real WebSocket handshake before judging shutdown. Both actor
  cores publish through the synchronized output wrapper, including during signal handling.
- **Application bootstrap and TaskManager status display match their actual contracts (Huly QB-799, QB-800).**
  TaskManager's psql script connects to the new database on one meta-command and grants its
  application role neither CREATEDB nor SUPERUSER. Auction House creates its application role
  before the database and makes that role the owner, so PostgreSQL 16 permits schema creation.
  Completed TaskManager cards color their own status stripe.
- **The REST books lesson accepts its documented availability filter (Huly QB-789).**
  `available=true/false` and `1/0` filter by the stored boolean; other nonempty values receive
  HTTP 400 with a JSON explanation.
- **The HTTP/2 page labels observations instead of invented transport metrics (Huly QB-844).**
  Response time includes body consumption, the run rate uses elapsed wall time, body size is
  decoded UTF-8 bytes, and concurrency counts pending fetch calls. The server returns its real
  HTTP/2 stream ID when present; the page does not claim connection reuse from that ID.
- **The hot-path lesson qualifies its raw tick samples (Huly QB-769).** It no longer concludes
  that same-core and cross-core costs match or that allocations are zero from three undrained
  single-run enqueue loops.
- **Redis and WebSocket lessons describe their actual execution and storage costs (Huly QB-861, QB-863).**
  An unawaited Redis command sends nothing, detached task exceptions are reported, and the Redis
  cardinality example compares `MEMORY USAGE` on both keys, labels Redis's sampled set estimate,
  and describes `BITPOS` as a scan.
  The chat guide explains the per-session serialization performed by `stream_if()`.
- **Actor, I/O and coroutine lessons match current contracts (Huly QB-860, QB-784, QB-778, QB-770).**
  The hot-path lesson separates event destruction from byte relocation; the crypto lesson
  reports Argon2id or its PBKDF2 fallback, checks both password verdicts and exits nonzero
  if either fails. The I/O guide records `eos` before graceful disconnection. The coroutine guide
  describes pass-zero time, event-driven `ready_async`, value-owned context IDs and current generator names.
- **HTTP lesson text matches its client, routes and bundled certificate (Huly QB-843).** The
  HTTP/1.1 batch timing is described as this client's serial queue policy, not a protocol ban
  on pipelining. The HTTP/2 startup guide, cookie scope and attributes, certificate metadata,
  and `/` versus `/static` routes now match the shipped source and files.
- **PostgreSQL lessons distinguish wire and application contracts (Huly QB-848).** The callback
  chain remains supported and may start work before `await()`; queued SQL still waits for
  ReadyForQuery between statements. Stream row callbacks run after each FETCH completes.
  Transaction notes distinguish server-side abort from local errors and zero-row updates, and
  `rows<T>()` is described as a lazy view that materialises tuples on dereference.
- **The HTTP static demo no longer serves stale framework metadata (Huly QB-847).** Its pages and
  sample data identify the C++20 language level and Apache-2.0 license, and label the sample version
  as illustrative instead of presenting a hand-written framework release.
- **The HTTP/2 demo's navigation and feature grid receive their intended styles (Huly QB-846).**
  Desktop and mobile selectors now use the same class names as the served HTML.
- **PostgreSQL lessons own the objects they remove (Huly QB-824).** Lessons 02, 04–07, 09 and 10
  use connection-local temporary tables; the trigger function in 07 is temporary too, and its
  notification channel is unique to the publisher session. The transactions lesson keeps a
  permanent table for its READ ONLY check but gives each run a random name and reports failed
  cleanup. The callback lesson stops on failed setup and qualifies reads with `pg_temp`. A disposable-cluster
  check proves same-named public tables and a public function survive, two runs of each lesson
  complete together, and run-owned tables are gone afterwards. `QB_EXAMPLE_PG_URI` selects the
  test database without editing and rebuilding the lessons.
- **The manual WebSocket chat client answers Close and reconnects (Huly QB-831, QB-833).**
  A peer Close now receives one masked Close reply. `/disconnect` and a peer Close release
  the old socket; `/connect` is refused until a pending Close has fully left the socket. The next
  handshake starts with clean buffers, a fresh HTTP parser and a new key. A loopback check
  covers backpressure, both reconnection paths and bounded exit.
- **Redis cache actor finishes after denied writes (Huly QB-829).** Each of five requests
  reports one result even when SET fails. The coordinator waits for all five, lets the worker
  clean up, and exits with failure instead of hanging or printing a success verdict.
- **HTTP account registration and login use the same demo password check (Huly QB-790).**
  Newly registered accounts accept their chosen password and reject another; seeded accounts keep their
  documented credentials. This in-memory example stores plain-text passwords and does not model production storage.
- **The HTTP upload page preserves special filenames and entered metadata (Huly QB-823).**
  It renders filenames, descriptions, tags and MIME types as text, encodes download/delete URL segments, and
  stores comma-separated form tags instead of replacing them with fixed values. Upload and list response paths
  and the `Location` header also encode the filename. Oversized tag fields are rejected before splitting or saving.
- **The Redis sliding-window example admits atomically and checks writes (Huly QB-826, QB-827).**
  One EVAL now purges, counts, adds a unique member and sets expiry on the server. Competing
  callers cannot consume the same slot; a failed add or expiry reports an error, with an expiry
  failure attempting to remove the added member. A disposable-Redis test covers concurrent calls,
  same-millisecond entries, expired windows, denied commands and failed EVAL/TTL replies.
- **The Redis cardinality example handles refused or short `BITFIELD` replies (Huly QB-828).**
  It prints `n/a` for an unavailable counter, completes cleanup and exits with a failed verdict
  instead of reading past the reply vector and crashing. The normal run now requires the
  measured counter line; its verdict checks the values.
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
- **The HTTP file API rejects path-shaped filenames before deletion or metadata lookup (Huly QB-987).**
  A decoded parent path can no longer delete a file beside `uploads`; GET, PUT and DELETE
  accept only one filename component and return 400 for separators, NUL, dot names or a Windows drive/colon.
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
